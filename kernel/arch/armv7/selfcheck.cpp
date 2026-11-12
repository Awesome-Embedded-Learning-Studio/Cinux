#include "kernel/arch/armv7/selfcheck.hpp"

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

}  // namespace cinux::armv7
