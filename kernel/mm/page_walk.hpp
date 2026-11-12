/**
 * @file    page_walk.hpp
 * @brief   The page-table walk algorithm, shared by both worlds.
 *
 * Map, unmap, and translate as free templates over a TableWorld: the walk
 * logic lives here exactly once, while each world supplies how a table
 * page is reached and where fresh table pages come from. How many levels
 * this machine walks, how wide a table is, and where each level reads its
 * index bits are declared in kernel/arch/page_walk.hpp; the algorithm
 * below iterates that shape and names no level count of its own. TLB
 * maintenance is deliberately outside: the walk edits tables, callers
 * own translations.
 *
 * @author  Charliechen114514
 * @date    2026-10-05
 * @version 0.3
 * @since   0.1.0
 * @ingroup kernel_mm
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <concepts>

#include "cinux/bit_ops/bitmask.hpp"
#include "kernel/arch/page_walk.hpp"

namespace cinux::mm::walk {

using cinux::arch::page::Entry;

/// The arch's table grammar, republished so walk callers name this header alone.
using cinux::arch::page::DescriptorIsLeaf;
using cinux::arch::page::DescriptorPresent;
using cinux::arch::page::EntryTarget;
using cinux::arch::page::MakePageEntry;
using cinux::arch::page::MakeTablePointer;
using cinux::arch::page::SmallLeafTarget;
using cinux::arch::page::TableTarget;
using cinux::arch::page::IndexRange;
using cinux::arch::page::kEntriesPerTable;
using cinux::arch::page::kLeafLevel;
using cinux::arch::page::kUpperLevels;
using cinux::arch::page::HasLargeLeaf;
using cinux::arch::page::IsLeafLevel;
using cinux::arch::page::kUserHalfSlots;
using cinux::arch::page::LargeLeafPhys;
using cinux::arch::page::LowBits;
using cinux::arch::page::SlotIndex;
using cinux::arch::page::WalkLevel;

/**
 * @brief   What a world must provide for the shared walk to run on it.
 * @note    table() turns a physical address into a writable table page;
 *          take_page() hands out one zeroed 4 KiB page as a fresh table,
 *          physical address 0 meaning refusal.
 * @since   0.1.0
 * @ingroup kernel_mm
 */
template <typename World>
concept TableWorld = requires(World& world, unsigned long physical) {
    { world.table(physical) } -> std::same_as<Entry*>;
    { world.take_page() } -> std::convertible_to<unsigned long>;
};

/**
 * @brief         Follows one table-pointer entry to the next table.
 *
 * @param[in,out] world   The table world the walk runs on.
 * @param[in]     slot    A present, non-large table entry.
 * @return        The next-level table.
 * @since         0.1.0
 * @ingroup       kernel_mm
 */
template <TableWorld World>
Entry* FollowTable(World& world, const Entry& slot) {
    return world.table(EntryTarget(slot));
}

/// Where the walk ended: the entry it landed on, and the level that
/// entry belongs to.
struct SlotAt {
    Entry*    entry;
    WalkLevel level;
};

/**
 * @brief         Walks to the slot an address resolves to, read-only.
 *
 * @param[in,out] world            The table world the walk runs on.
 * @param[in]     root             Physical address of the top-level table to walk.
 * @param[in]     virtual_address   The address whose slot is wanted.
 * @return        The slot and its level; entry is null when the walk
 *                fell off before reaching one.
 * @note          Stops early on a large-page leaf: the entry and its
 *                level are reported for the caller to decode.
 * @since         0.2.0
 * @ingroup       kernel_mm
 */
template <TableWorld World>
SlotAt FindSlot(World& world, unsigned long root, unsigned long virtual_address) {
    Entry* table = world.table(root);
    for (const WalkLevel kLevel : kUpperLevels) {
        Entry& slot = table[SlotIndex(virtual_address, kLevel)];
        if (!cinux::arch::page::DescriptorPresent(slot)) {
            return SlotAt{.entry = nullptr, .level = kLevel};
        }
        if (cinux::arch::page::DescriptorIsLeaf(slot, kLevel)) {
            return SlotAt{.entry = &slot, .level = kLevel};
        }
        table = FollowTable(world, slot);
    }
    return SlotAt{
        .entry = &table[SlotIndex(virtual_address, kLeafLevel)],
        .level = kLeafLevel,
    };
}

/**
 * @brief         Walks to the page table an address maps through,
 *                creating missing intermediate tables on the way.
 *
 * @param[in,out] world            The table world the walk runs on.
 * @param[in]     root             Physical address of the top-level table to walk.
 * @param[in]     virtual_address   The address whose page table is wanted.
 * @param[in]     leaf_flags        Flags each freshly created table
 *                                  carries in its pointer entry.
 * @return        The final page table, or null on refusal or on meeting
 *                a large-page leaf that cannot host a 4 KiB slot.
 * @since         0.2.0
 * @ingroup       kernel_mm
 */
template <TableWorld World>
Entry* EnsureLeafTable(World& world, unsigned long root, unsigned long virtual_address,
                       Entry leaf_flags) {
    Entry* table = world.table(root);
    for (const WalkLevel kLevel : kUpperLevels) {
        Entry& slot = table[SlotIndex(virtual_address, kLevel)];
        if (!cinux::arch::page::DescriptorPresent(slot)) {
            unsigned long const kFresh = world.take_page();
            if (kFresh == 0) {
                return nullptr;
            }
            slot  = cinux::arch::page::MakeTablePointer(
                    kFresh, cinux::arch::page::kWritable | leaf_flags);
            table = world.table(kFresh);
        } else if (cinux::arch::page::DescriptorIsLeaf(slot, kLevel)) {
            return nullptr;
        } else {
            table = FollowTable(world, slot);
        }
    }
    return table;
}

/**
 * @brief         Maps one 4 KiB page, creating missing intermediate
 *                tables from the world's supply.
 *
 * @param[in,out] world            The table world the walk runs on.
 * @param[in]     root             Physical address of the top-level table to walk.
 * @param[in]     virtual_address   Target address; the low bits of the
 *                                  physical side are dropped by the entry.
 * @param[in]     physical          Physical page to map.
 * @return        true when the final entry was written.
 * @note          Writes present-plus-writable entries throughout; the
 *                kernel-side face. Refuses to split a large-page leaf it
 *                meets mid-walk; that collision is the caller's policy
 *                problem.
 * @warning       None
 * @throws        None
 * @since         0.1.0
 * @ingroup       kernel_mm
 */
template <TableWorld World>
bool MapPage(World& world, unsigned long root, unsigned long virtual_address,
             unsigned long physical) {
    Entry* const kTable = EnsureLeafTable(world, root, virtual_address, Entry{});
    if (kTable == nullptr) {
        return false;
    }
    kTable[SlotIndex(virtual_address, kLeafLevel)] =
        cinux::arch::page::MakePageEntry(physical, cinux::arch::page::kWritable);
    return true;
}

/**
 * @brief         Maps one 4 KiB page ring 3 may reach.
 *
 * @param[in,out] world            The table world the walk runs on.
 * @param[in]     root             Physical address of the top-level table to walk.
 * @param[in]     virtual_address   Target address; the address side of
 *                                  the leaf decides the slot.
 * @param[in]     leaf             The finished leaf entry: the caller
 *                                  pairs the frame with its terms via
 *                                  MakeTableEntry before the walk.
 * @return        true when the final entry was written.
 * @note          Same walk with kUser on every level it touches —
 *                including each freshly created table, because a
 *                clearance lost at any one level denies the walk below
 *                it. Only the final entry takes the caller's leaf: a
 *                table pointer's write bit is a supply detail, the
 *                leaf's bits are the page's law.
 * @warning       None
 * @throws        None
 * @since         0.3.0
 * @ingroup       kernel_mm
 */
template <TableWorld World>
bool MapUserPage(World& world, unsigned long root, unsigned long virtual_address, Entry leaf) {
    Entry* const kTable = EnsureLeafTable(world, root, virtual_address, cinux::arch::page::kUser);
    if (kTable == nullptr) {
        return false;
    }
    kTable[SlotIndex(virtual_address, kLeafLevel)] = leaf;
    return true;
}

/**
 * @brief         Clears one 4 KiB mapping and reports what was there.
 *
 * @param[in,out] world            The table world the walk runs on.
 * @param[in]     root             Physical address of the top-level table to walk.
 * @param[in]     virtual_address   The page to unmap.
 * @return        The entry that occupied the slot, or a zero entry when
 *                the walk fell off before reaching one.
 * @note          Table-only: flushing the translation is the caller's
 *                duty, because only the caller knows which world ran.
 * @warning       None
 * @throws        None
 * @since         0.1.0
 * @ingroup       kernel_mm
 */
template <TableWorld World>
Entry UnmapPage(World& world, unsigned long root, unsigned long virtual_address) {
    SlotAt const kSlot = FindSlot(world, root, virtual_address);
    if (kSlot.entry == nullptr || kSlot.level != kLeafLevel) {
        return Entry{};
    }
    Entry const kOld = *kSlot.entry;
    *kSlot.entry     = Entry{};
    return kOld;
}

/**
 * @brief         Translates a virtual address to its physical byte.
 *
 * @param[in,out] world            The table world the walk runs on.
 * @param[in]     root             Physical address of the top-level table to walk.
 * @param[in]     virtual_address   The byte to resolve.
 * @return        Physical address of the same byte, or 0 when unmapped.
 * @note          Reads through 1 GiB, 2 MiB, and 4 KiB leaves alike:
 *                whichever level turns present-with-large ends the walk
 *                and its field supplies the physical base.
 * @warning       None
 * @throws        None
 * @since         0.2.0
 * @ingroup       kernel_mm
 */
template <TableWorld World>
unsigned long TranslatePage(World& world, unsigned long root, unsigned long virtual_address) {
    SlotAt const kSlot = FindSlot(world, root, virtual_address);
    if (kSlot.entry == nullptr || !cinux::arch::page::DescriptorPresent(*kSlot.entry)) {
        return 0;
    }
    if (!IsLeafLevel(kSlot.level)) {
        if (!HasLargeLeaf(kSlot.level)) {
            return 0;
        }
        auto const kField = LargeLeafPhys(kSlot.level);
        Entry      physical{};
        physical.deposit(kField, kSlot.entry->extract(kField));
        return physical.raw | LowBits(virtual_address, IndexRange(kSlot.level).low);
    }
    return cinux::arch::page::SmallLeafTarget(*kSlot.entry) |
           LowBits(virtual_address, IndexRange(kLeafLevel).low);
}

}  // namespace cinux::mm::walk
