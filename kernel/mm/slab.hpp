/**
 * @file kernel/mm/slab.hpp
 * @brief SLAB small-object allocator (F2-M7b)
 *
 * Layers fast size-classed allocation on top of the PMM (buddy).  General
 * caches serve power-of-two object sizes 16..2048 bytes; a request larger than
 * kSlabMaxObj is not slab-eligible and the kmalloc layer routes it to the buddy
 * + direct-map (F2-M7b batch 2).  Each cache owns a list of slab pages; a slab
 * is a single 4 KB page carved into fixed-size objects with an intrusive free
 * list -- the link lives in the freed object's own first word, so there is no
 * external per-object metadata.
 *
 * CRITICAL (GOTCHA #13/#14): the intrusive free-list link is written INSIDE a
 * slab page.  Such pages MUST be ordinary 4 KB mappings in the KMEM_SLAB
 * region -- never the direct-map huge window.  A sub-page write inside a huge
 * page is not reliably read back under WSL2 nested-KVM (the F2-M7 buddy
 * intrusive free-list crashed for exactly this reason).  4 KB mappings are safe.
 *
 * Locking follows the PMM/VMM idiom: public alloc/free take the spinlock with
 * interrupts disabled (Spinlock::irq_guard, which is idempotent under IF=0, so
 * the page-fault path that allocates a CachedPage via operator new works).
 * Growing a cache maps a new page with g_pmm.alloc_page + g_vmm.map, the same
 * IF=0-safe sequence Heap::expand proved.
 *
 * Namespace: cinux::mm
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "kernel/proc/sync.hpp"

namespace cinux::mm {

struct SlabCache;

/// In-page header placed at the start of every slab page.  Doubly linked so a
/// slab can move between its cache's partial/full lists in O(1).
struct SlabHeader {
    SlabCache*  cache;     ///< owning cache
    SlabHeader* next;
    SlabHeader* prev;
    void*       freelist;  ///< head of the intrusive free list (nullptr when full)
    uint16_t    inuse;     ///< objects currently allocated out of this slab
    uint16_t    total;     ///< objects this slab can hold
};

/// A size-classed cache of slab pages.  General caches are created by init();
/// dedicated caches (Task/Inode/...) arrive in batch 3.
struct SlabCache {
    size_t      obj_size;       ///< bytes per object (power of two, >= 16)
    uint16_t    objs_per_slab;  ///< capacity of one slab page
    const char* name;
    SlabHeader* partial;        ///< slabs with at least one free slot (alloc target)
    SlabHeader* full;           ///< completely used slabs
    uint32_t    slab_count;     ///< live slab pages in this cache
    void        (*ctor)(void*); ///< per-object constructor (dedicated caches)
    void        (*dtor)(void*); ///< per-object destructor  (dedicated caches)
};

/// Smallest general cache is 16 B so that every returned pointer satisfies the
/// default operator-new alignment (__STDCPP_DEFAULT_NEW_ALIGNMENT__, 16 on
/// x86-64).  An 8 B class would force 16-aligned slots anyway and waste half
/// the page, so it is dropped.
constexpr int    kSlabMinObjLog2 = 4;   ///< log2(16)
constexpr int    kSlabMaxObjLog2 = 11;  ///< log2(2048)
constexpr size_t kSlabMinObj     = size_t{1} << kSlabMinObjLog2;
constexpr size_t kSlabMaxObj     = size_t{1} << kSlabMaxObjLog2;
constexpr int    kNumCaches      = kSlabMaxObjLog2 - kSlabMinObjLog2 + 1;  // 8

/**
 * @brief SLAB allocator backed by per-size-class caches of 4 KB slab pages
 */
class SlabAllocator {
public:
    /// Initialise the general caches and remember the KMEM_SLAB virtual window.
    /// No pages are mapped until the first allocation.
    void init(uint64_t virt_base, uint64_t region_size);

    /// Allocate @p size bytes from the matching general cache.  Returns nullptr
    /// for size == 0, size > kSlabMaxObj (not slab-eligible), or OOM.  The
    /// returned memory is zeroed (matching Heap::alloc).
    void* alloc(size_t size);

    /// Free an object previously returned by alloc().  No-op for nullptr or for
    /// pointers outside the slab window.
    void free(void* ptr);

    // ---- dedicated caches (batch 3) ----
    // SlabCache* create_cache(const char* name, size_t obj_size,
    //                         void (*ctor)(void*), void (*dtor)(void*));
    // void* cache_alloc(SlabCache* cache);
    // void cache_free(SlabCache* cache, void* obj);

    /// Number of slab pages currently mapped (diagnostics only).
    uint32_t total_slab_pages() const { return slab_pages_; }

private:
    void* alloc_locked(size_t size);
    void  free_locked(void* ptr);

    /// Map a fresh 4 KB slab page for @p cache and thread its objects onto the
    /// free list.  Returns nullptr on OOM (window exhausted or PMM dry).
    SlabHeader* grow_cache(SlabCache& cache);

    static int  size_to_index(size_t size);  ///< cache index, or -1 if not eligible
    static void list_push_front(SlabHeader** head, SlabHeader* s);
    static void list_remove(SlabHeader** head, SlabHeader* s);

    SlabCache          caches_[kNumCaches];
    cinux::proc::Spinlock lock_;
    uint64_t           slab_base_{};
    uint64_t           slab_brk_{};
    uint64_t           slab_end_{};
    uint32_t           slab_pages_{};
};

/// Global SlabAllocator instance.
extern SlabAllocator g_slab;

// ============================================================
// kmalloc / kfree -- the universal kernel allocator (F2-M7b batch 2)
// ============================================================

/**
 * @brief Allocate @p size bytes aligned to at least @p align
 *
 * Small requests (effective size = max(size, align) <= kSlabMaxObj) are served
 * from the slab caches.  Larger ones draw whole buddy pages via the permanent
 * direct map -- no per-alloc map/unmap (GOTCHA #7/#13).  Either way the memory
 * is zeroed (no stale-data leak).  Returns nullptr on OOM or size == 0.
 *
 * Free with kfree(); global operator new/delete forward here.
 */
void* kmalloc(size_t size, size_t align = 16);

/**
 * @brief Free memory returned by kmalloc() / operator new
 *
 * Routes by address: the slab window goes to the slab allocator, the direct-map
 * window goes back to the buddy (count is immaterial -- the buddy records the
 * order).  No-op for nullptr or pointers outside both windows.
 */
void kfree(void* ptr);

}  // namespace cinux::mm
