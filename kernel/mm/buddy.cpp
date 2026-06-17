/**
 * @file kernel/mm/buddy.cpp
 * @brief Buddy-system physical page allocator implementation (F2-M7)
 *
 * Namespace: cinux::mm
 */

#include "kernel/mm/buddy.hpp"

namespace cinux::mm {

// ============================================================
// Initialisation
// ============================================================

void BuddyAllocator::init(uint64_t base_phys, uint64_t total_pages, uint8_t* order_storage) {
    base_phys_   = base_phys;
    total_pages_ = total_pages;
    order_       = order_storage;
    free_pages_  = 0;
    for (int o = 0; o <= kMaxOrder; o++) {
        free_lists_[o] = nullptr;
    }
    for (uint64_t p = 0; p < total_pages; p++) {
        order_[p] = kNotAllocatedHead;  // nothing allocated yet
    }
}

int BuddyAllocator::largest_fitting_order(uint64_t page, uint64_t remaining) {
    // Greedily take the largest order whose block is both aligned at @p page and
    // fits within @p remaining.  This keeps the buddy alignment invariant while
    // naturally absorbing unaligned region edges as smaller-order blocks.
    int o = 0;
    while (o < kMaxOrder) {
        uint64_t next_size = 1ULL << (o + 1);
        if ((page & (next_size - 1)) == 0 && remaining >= next_size) {
            o++;
        } else {
            break;
        }
    }
    return o;
}

void BuddyAllocator::mark_free_region(uint64_t base_page, uint64_t count) {
    uint64_t p   = base_page;
    uint64_t end = base_page + count;
    while (p < end) {
        int o = largest_fitting_order(p, end - p);
        push_free(p, o);
        free_pages_ += (1ULL << o);
        p += (1ULL << o);
    }
}

// ============================================================
// Free-list primitives (no free-page accounting here)
// ============================================================

void BuddyAllocator::push_free(uint64_t page, int order) {
    FreeBlock* b       = page_to_block(page);
    b->next            = free_lists_[order];
    free_lists_[order] = b;
    // order_[page] stays kNotAllocatedHead; free blocks are tracked by the lists.
}

uint64_t BuddyAllocator::pop_free(int order) {
    FreeBlock* b = free_lists_[order];
    if (b == nullptr) {
        return kInvalidPage;
    }
    free_lists_[order] = b->next;
    uint64_t virt      = reinterpret_cast<uint64_t>(b);
    return (virt - base_phys_ - cinux::arch::KERNEL_VMA) / cinux::arch::PAGE_SIZE;
}

bool BuddyAllocator::remove_free(uint64_t page, int order) {
    FreeBlock* target = page_to_block(page);
    FreeBlock* prev   = nullptr;
    FreeBlock* cur    = free_lists_[order];
    while (cur != nullptr) {
        if (cur == target) {
            if (prev == nullptr) {
                free_lists_[order] = cur->next;
            } else {
                prev->next = cur->next;
            }
            return true;
        }
        prev = cur;
        cur  = cur->next;
    }
    return false;
}

void BuddyAllocator::mark_head_allocated(uint64_t page, int order) {
    uint64_t size = 1ULL << order;
    order_[page]  = static_cast<uint8_t>(order);
    for (uint64_t i = 1; i < size && (page + i) < total_pages_; i++) {
        order_[page + i] = kNotAllocatedHead;  // mark interior, so stray frees are no-ops
    }
}

// ============================================================
// Allocation
// ============================================================

uint64_t BuddyAllocator::alloc_order(int order) {
    if (order < 0 || order > kMaxOrder) {
        return kInvalidPage;
    }

    // Find the smallest free list at or above the requested order.
    int o = order;
    while (o <= kMaxOrder && free_lists_[o] == nullptr) {
        o++;
    }
    if (o > kMaxOrder) {
        return kInvalidPage;  // OOM
    }

    uint64_t page = pop_free(o);

    // Split the larger block down to the requested order, returning each upper
    // half to a lower-order free list.  The lower half (head @p page) is kept.
    while (o > order) {
        o--;
        push_free(page + (1ULL << o), o);
    }

    mark_head_allocated(page, order);
    free_pages_ -= (1ULL << order);
    return page;
}

// ============================================================
// Free + coalescing
// ============================================================

void BuddyAllocator::free(uint64_t page) {
    if (page >= total_pages_) {
        return;
    }
    uint8_t recorded = order_[page];
    if (recorded == kNotAllocatedHead) {
        return;  // not an allocated head: double-free / interior / reserved
    }
    int order = static_cast<int>(recorded);

    // Return the block's pages to the free count and clear its head marker.
    free_pages_ += (1ULL << order);
    uint64_t size = 1ULL << order;
    for (uint64_t i = 0; i < size && (page + i) < total_pages_; i++) {
        order_[page + i] = kNotAllocatedHead;
    }

    // Coalesce with buddies as far as the alignment invariant allows.  The buddy
    // is mergeable only if it is currently a free block of exactly this order,
    // which remove_free() decides by unlinking it from that order's list.  If the
    // buddy is allocated or not a matching free block, stop -- this is correct
    // (never merges wrongly) and at worst leaves benign fragmentation.  Either
    // way the free-page count above is already exact.
    while (order < kMaxOrder) {
        uint64_t b = buddy_of(page, order);
        if (b >= total_pages_) {
            break;  // buddy outside the managed range (region edge)
        }
        if (!remove_free(b, order)) {
            break;  // buddy not a free block of this order -> cannot merge
        }
        page = (b < page) ? b : page;  // merged block head is the lower address
        order++;
    }

    push_free(page, order);
}

}  // namespace cinux::mm
