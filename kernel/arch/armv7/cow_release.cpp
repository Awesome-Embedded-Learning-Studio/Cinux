/**
 * @file    cow_release.cpp
 * @brief   Frame release and the share ledger for the armv7 lane.
 *
 * The page-table half of copy-on-write lives here: marking a leaf
 * shared, and the release walk's bookkeeping. The share counting itself
 * is the allocator's, one mechanism for every port, and it sits in the
 * neutral layer beside the clone walk that uses it.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.3
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/mm/cow.hpp"

#include "cinux/assert.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/space_control.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/hardware_frames.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vmm.hpp"

namespace cinux::mm::cow {


bool MarkPageShared(unsigned long root, unsigned long virtual_address) {
    using cinux::arch::page::Entry;
    using cinux::arch::page::kReadOnly;
    using cinux::arch::page::kUser;
    using cinux::arch::page::kWritable;

    cinux::mm::KernelTables world;
    const walk::SlotAt     kSlot = walk::FindSlot(world, root, virtual_address);
    if (kSlot.entry == nullptr || !walk::IsLeafLevel(kSlot.level)) {
        return false;
    }
    const Entry kLeaf = *kSlot.entry;
    if (!cinux::arch::page::IsPresent(kLeaf)) {
        return false;
    }
    const unsigned long kTarget = walk::EntryTarget(kLeaf);
    // The ledger counts address spaces, not marks: a leaf that still
    // carries write permission is joining its first companion, and that
    // transition is the one retain a clone event adds.
    Entry terms = cinux::arch::page::LeafTermsOf(kLeaf);
    if (terms.has(kWritable)) {
        RetainFrame(kTarget);
    }
    terms        = (terms & ~kWritable) | kUser | kReadOnly;
    *kSlot.entry = cinux::arch::page::MakePageEntry(kTarget, terms);
    cinux::arch::space::InvalidatePage(virtual_address);
    return true;
}

bool MarkChildShared(unsigned long root, unsigned long virtual_address, bool parent_was_shared) {
    using cinux::arch::page::Entry;
    using cinux::arch::page::kReadOnly;
    using cinux::arch::page::kUser;
    using cinux::arch::page::kWritable;

    cinux::mm::KernelTables world;
    const walk::SlotAt     kSlot = walk::FindSlot(world, root, virtual_address);
    if (kSlot.entry == nullptr || !walk::IsLeafLevel(kSlot.level)) {
        return false;
    }
    const Entry kLeaf = *kSlot.entry;
    if (!cinux::arch::page::IsPresent(kLeaf)) {
        return false;
    }
    const unsigned long kTarget = walk::EntryTarget(kLeaf);
    // A fresh clone just counted the companion on the parent's mark. A
    // fork of a fork meets the parent already read-only, so the new
    // child space is the one that grows the count here.
    if (parent_was_shared) {
        RetainFrame(kTarget);
    }
    Entry terms = cinux::arch::page::LeafTermsOf(kLeaf);
    terms       = (terms & ~kWritable) | kUser | kReadOnly;
    *kSlot.entry = cinux::arch::page::MakePageEntry(kTarget, terms);
    cinux::arch::space::InvalidatePage(virtual_address);
    return true;
}

void ReleaseFrame(unsigned long long frame) {
    // A device's own frame is not the allocator's to take back: the
    // panel, for one, outlives every mapping that showed it.
    if (cinux::mm::IsHardwarePage(static_cast<unsigned long>(frame))) {
        return;
    }
    if (Pmm::self().release_frame(cinux::base::PhysAddr{static_cast<unsigned long>(frame)}) != 0) {
        return;  // someone else still walks it
    }
    Pmm::self().free_page(cinux::base::PhysAddr{static_cast<unsigned long>(frame)});
}

}  // namespace cinux::mm::cow
