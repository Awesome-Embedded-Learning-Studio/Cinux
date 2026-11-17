/**
 * @file    kernel_tables.cpp
 * @brief   The two kernel-table steps every port words the same way.
 *
 * Reaching a table behind the direct map and drawing one zeroed page from
 * the ledger need nothing but the port's direct map, its page manager and
 * its table shape. Both ports had written them out separately; the steps
 * live here once, and each port keeps only what it really decides -- the
 * top level's size and alignment (take_root/release_root).
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_mm
 * @copyright Copyright (c) 2026
 */

#include "kernel/mm/vmm.hpp"

#include "cinux/memory.hpp"
#include "cinux/ptr.hpp"
#include "kernel/arch/page.hpp"
#include "cinux/port/page_walk_shape.hpp"
#include "kernel/arch/space.hpp"
#include "kernel/mm/pmm.hpp"

namespace cinux::mm {

cinux::arch::page::Entry* KernelTables::table(unsigned long physical) {
    return cinux::base::PtrAt<cinux::arch::page::Entry>(
        cinux::arch::space::DirectMapVirt(physical));
}

unsigned long KernelTables::take_page() {
    cinux::base::PhysAddr const kFresh = Pmm::self().allocate_page();
    if (kFresh == cinux::base::PhysAddr{}) {
        return 0;
    }
    cinux::base::SetBytes(
        cinux::base::PtrAt<void>(cinux::arch::space::DirectMapVirt(kFresh.raw)), 0,
        sizeof(cinux::arch::page::Entry) * cinux::arch::page::kEntriesPerTable);
    return kFresh.raw;
}

}  // namespace cinux::mm
