/**
 * @file    cow_release.cpp
 * @brief   Frame release and the share ledger for the armv7 lane.
 *
 * Exclusive frames go straight back to the allocator. Shared frames are
 * counted in a small table: the last owner frees, everyone else just
 * drops a count. No copy happens yet; that is the fault path's job and
 * it does not exist on this port.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.3
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/cow_ledger.hpp"

#include "cinux/assert.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/space_control.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vmm.hpp"

namespace cinux::mm::cow {

namespace {

constexpr unsigned long kLedgerEntries = 128;

struct SharedFrame {
    unsigned long long frame;
    unsigned long      count;
};

SharedFrame g_ledger[kLedgerEntries];

SharedFrame* find_shared(unsigned long long frame) {
    for (auto& entry : g_ledger) {
        if (entry.frame == frame) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

void RetainFrame(unsigned long long frame) {
    if (SharedFrame* entry = find_shared(frame); entry != nullptr) {
        ++entry->count;
        return;
    }
    for (auto& entry : g_ledger) {
        if (entry.frame == 0) {
            entry.frame = frame;
            entry.count = 2;  // the owner that asked plus the one before
            return;
        }
    }
}

unsigned long FrameShares(unsigned long long frame) {
    const SharedFrame* entry = find_shared(frame);
    return entry == nullptr ? 0 : entry->count;
}

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
    if (SharedFrame* entry = find_shared(frame); entry != nullptr) {
        if (entry->count > 1) {
            --entry->count;
            return;
        }
        entry->frame = 0;
        entry->count = 0;
    }
    Pmm::self().free_page(cinux::base::PhysAddr{static_cast<unsigned long>(frame)});
}

}  // namespace cinux::mm::cow
