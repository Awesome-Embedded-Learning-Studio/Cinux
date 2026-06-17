/**
 * @file kernel/mm/buddy.hpp
 * @brief Buddy-system physical page allocator (F2-M7)
 *
 * Replaces the PMM's flat bitmap with power-of-two free lists.  Each free
 * block lives on an order-keyed singly-linked list whose link pointer is stored
 * inside the block's own (direct-mapped) page, so there is zero per-block
 * metadata overhead -- the same "use the page's own direct map, never unmap it"
 * model as the DmaPool / PageCache (GOTCHA #7).
 *
 * The allocation order of each block is recorded in a caller-provided byte array
 * (one byte per page) at the block's head page.  This makes free() authoritative:
 * callers need not remember how large a block was -- the recorded order drives
 * coalescing.  A sentinel marks every page that is not an allocated head (free
 * pages, free/allocated interiors, reserved RAM), so double-free and
 * free-of-interior are safe no-ops.
 *
 * The allocator is NOT thread-safe by itself.  The owning PMM serialises access
 * (its spinlock for normal callers, or exclusion-via-IF=0 for the page-fault
 * path), matching the old PMM's _locked contract (GOTCHA #11).  Buddy methods
 * therefore never take a lock -- "caller holds exclusion" semantics.
 *
 * Namespace: cinux::mm
 */

#pragma once

#include <stdint.h>

#include "kernel/arch/x86_64/memory_layout.hpp"
#include "kernel/arch/x86_64/paging_config.hpp"

namespace cinux::mm {

class BuddyAllocator {
public:
    /// Largest block is 2^kMaxOrder pages (8 MiB at 4 KiB pages) -- comfortably
    /// above every existing alloc_pages() caller (DMA pool, kernel stacks).
    static constexpr int kMaxOrder = 11;

    /// Sentinel stored in the order array for any page that is not the head of an
    /// allocated block (free pages, free/allocated interiors, reserved RAM).
    static constexpr uint8_t kNotAllocatedHead = 0xFF;

    /// Returned by alloc_order() when no block of the requested order fits.
    static constexpr uint64_t kInvalidPage = UINT64_MAX;

    /// Set up an empty allocator over page indices [0, total_pages) of
    /// @p base_phys.  No pages are free yet -- call mark_free_region() for each
    /// usable span.  @p order_storage must hold at least total_pages bytes and
    /// outlive the allocator (the PMM places it beside the kernel image, exactly
    /// where the old bitmap lived).
    void init(uint64_t base_phys, uint64_t total_pages, uint8_t* order_storage);

    /// Mark page indices [base_page, base_page + count) as free, splitting the
    /// span into the largest buddy blocks that keep the power-of-two alignment
    /// invariant.  Pages never marked free (kernel image, metadata, MMIO holes)
    /// simply stay invisible to the allocator -- they are never handed out.
    void mark_free_region(uint64_t base_page, uint64_t count);

    /// Allocate a block of 2^order pages.  Returns the head page index, or
    /// kInvalidPage on OOM / bad order.  Caller holds exclusion.
    uint64_t alloc_order(int order);

    /// Free the block whose head is @p page.  The order is taken from recorded
    /// metadata (authoritative), and the block is coalesced with its buddies as
    /// far as the alignment invariant allows.  No-op if @p page is not an
    /// allocated head (double-free, interior page, reserved page, out of range).
    void free(uint64_t page);

    uint64_t free_pages() const { return free_pages_; }
    uint64_t total_pages() const { return total_pages_; }

private:
    /// Intrusive free-list node, stored in the block's own direct-mapped page.
    struct FreeBlock {
        FreeBlock* next;
    };

    FreeBlock* free_lists_[kMaxOrder + 1]{};
    uint8_t*   order_{nullptr};  ///< [page] -> order (allocated head) or kNotAllocatedHead
    uint64_t   base_phys_{0};
    uint64_t   total_pages_{0};
    uint64_t   free_pages_{0};

    /// Direct-mapped kernel virtual address of a given page index.
    FreeBlock* page_to_block(uint64_t page) const {
        return reinterpret_cast<FreeBlock*>(base_phys_ + page * cinux::arch::PAGE_SIZE +
                                            cinux::arch::KERNEL_VMA);
    }

    static uint64_t buddy_of(uint64_t page, int order) { return page ^ (1ULL << order); }

    void       push_free(uint64_t page, int order);
    uint64_t   pop_free(int order);  ///< returns kInvalidPage if the list is empty
    bool       remove_free(uint64_t page, int order);
    void       mark_head_allocated(uint64_t page, int order);
    static int largest_fitting_order(uint64_t page, uint64_t remaining);
};

}  // namespace cinux::mm
