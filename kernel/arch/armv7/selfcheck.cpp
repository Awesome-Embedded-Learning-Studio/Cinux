#include "kernel/arch/armv7/selfcheck.hpp"

#include "kernel/mm/heap.hpp"
#include "kernel/mm/pmm.hpp"

namespace cinux::armv7 {

bool RunPmmSelfCheck() {
    auto& pmm = cinux::mm::Pmm::self();
    const unsigned long before = pmm.free_page_count();

    const cinux::base::PhysAddr first = pmm.allocate_page();
    if (first.raw == 0) {
        return false;
    }
    const cinux::base::PhysAddr second = pmm.allocate_page();
    if (second.raw == 0 || second.raw == first.raw) {
        pmm.free_page(first);
        return false;
    }

    // Identity-mapped: the physical address is its own virtual address.
    auto* const page = reinterpret_cast<volatile unsigned long*>(first.raw);
    *page            = 0x5A5A5A5AUL;
    if (*page != 0x5A5A5A5AUL) {
        pmm.free_page(first);
        pmm.free_page(second);
        return false;
    }

    pmm.free_page(first);
    pmm.free_page(second);
    if (pmm.free_page_count() != before) {
        return false;
    }

    // An order above kMaxOrder must be refused, not served.
    return pmm.allocate_pages(20).raw == 0;
}

namespace {
alignas(16) unsigned char g_heap_arena[64 * 1024];
}  // namespace

bool RunHeapSelfCheck() {
    auto& heap = cinux::mm::Heap::self();
    if (!heap.init(reinterpret_cast<unsigned long>(g_heap_arena), sizeof(g_heap_arena))) {
        return false;
    }
    const unsigned long free_at_start = heap.free_bytes();

    auto* const first  = static_cast<unsigned char*>(heap.allocate(128));
    auto* const second = static_cast<unsigned char*>(heap.allocate(256));
    if (first == nullptr || second == nullptr || first == second) {
        return false;
    }
    if ((reinterpret_cast<unsigned long>(first) % 16U) != 0) {
        return false;
    }
    first[0]   = 0x11;
    second[0]  = 0x22;
    if (first[0] != 0x11 || second[0] != 0x22) {
        return false;
    }
    if (!heap.free(first) || !heap.free(second)) {
        return false;
    }
    if (heap.free_bytes() != free_at_start) {
        return false;
    }
    // Double free must be refused rather than corrupting the list.
    return !heap.free(first);
}

}  // namespace cinux::armv7
