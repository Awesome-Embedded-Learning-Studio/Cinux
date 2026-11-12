/**
 * @file    clone_space.hpp
 * @brief   Cloning a user subtree: shared leaves, read-only on both sides.
 *
 * The armv7 user world is one first-level slot holding a second-level
 * table of small pages. Cloning walks that table once: every present
 * leaf is retained, the child gets its own second-level table pointing
 * at the same frames, and both sides drop to user read-only so the next
 * write on either side takes a copy-on-write fault.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/armv7/cow_ledger.hpp"
#include "kernel/arch/armv7/page_entry.hpp"
#include "kernel/arch/armv7/page_walk_shape.hpp"
#include "kernel/arch/armv7/space_control.hpp"
#include "kernel/arch/armv7/space_layout.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/vmm.hpp"

namespace cinux::armv7 {

/**
 * @brief         Clones the user subtree of `parent` into `child`.
 * @param[in]     parent   The space being cloned; its leaves become read-only.
 * @param[in,out] child    A space whose init() already ran.
 * @return        true when every user leaf was shared.
 * @note          Only small-page leaves are supported; a section in the
 *                user window refuses the clone instead of being guessed at.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline bool CloneUserSpace(cinux::mm::AddressSpace& parent, cinux::mm::AddressSpace& child) {
    using cinux::arch::page::DescriptorIsLeaf;
    using cinux::arch::page::DescriptorPresent;
    using cinux::arch::page::Entry;
    using cinux::arch::page::kLeafLevel;
    using cinux::arch::page::WalkLevel;
    using cinux::arch::page::kReadOnly;
    using cinux::arch::page::kUser;
    using cinux::arch::page::MakePageEntry;
    using cinux::arch::page::MakeTablePointer;
    using cinux::arch::page::SmallLeafTarget;
    using cinux::arch::page::TableTarget;

    cinux::mm::KernelTables tables;
    auto* const parent_root = reinterpret_cast<Entry*>(parent.root());
    auto* const child_root  = reinterpret_cast<Entry*>(child.root());
    for (unsigned long index = 0; index < 4096; ++index) {
        if (!cinux::arch::space::IsUserSlot(index)) {
            continue;
        }
        Entry& slot = parent_root[index];
        if (!DescriptorPresent(slot)) {
            continue;
        }
        if (DescriptorIsLeaf(slot, WalkLevel::kL1)) {
            return false;  // a section in the user window: not this lane's shape
        }
        const unsigned long kChildTable = tables.take_page();
        if (kChildTable == 0) {
            return false;
        }
        auto* const parent_table = reinterpret_cast<Entry*>(TableTarget(slot));
        auto* const child_table  = reinterpret_cast<Entry*>(kChildTable);
        for (unsigned long leaf = 0; leaf < 256; ++leaf) {
            Entry& parent_leaf = parent_table[leaf];
            if (!DescriptorPresent(parent_leaf)) {
                child_table[leaf] = Entry{0};
                continue;
            }
            if (!DescriptorIsLeaf(parent_leaf, kLeafLevel)) {
                return false;
            }
            const unsigned long kFrame = SmallLeafTarget(parent_leaf);
            cinux::mm::cow::RetainFrame(kFrame);
            child_table[leaf]  = MakePageEntry(kFrame, kUser | kReadOnly);
            parent_leaf        = MakePageEntry(kFrame, kUser | kReadOnly);
        }
        child_root[index] = MakeTablePointer(kChildTable, Entry{0});
    }
    cinux::arch::space::InvalidateAll();
    return true;
}

}  // namespace cinux::armv7
