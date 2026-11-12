/**
 * @file    page_walk_shape.hpp
 * @brief   The armv7 walk shape: two levels, sections and small pages.
 *
 * Level 1 holds 4096 entries twelve index bits wide; level 2 holds 256
 * entries eight bits wide. Sections are leaves at level 1, small pages
 * are leaves at level 2.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/bit_ops/bitmask.hpp"
#include "kernel/arch/armv7/page_entry.hpp"

namespace cinux::arch::page {

using cinux::base::bit::BitRange;

/// Entries one second-level table holds; the level counts differ on this
/// port, so the walk asks EntriesAt() per level.
constexpr unsigned int kEntriesPerTable = 256;

/// The level 4 KiB leaves occupy.
constexpr WalkLevel kLeafLevel = WalkLevel::kL2;

/// The levels the walk descends through before the leaf table.
constexpr WalkLevel kUpperLevels[1] = {WalkLevel::kL1};

/**
 * @brief         Bit range of one level's index.
 * @param[in]     level   The level asked about.
 * @return        Twelve bits at level 1, eight at level 2.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr BitRange IndexRange(WalkLevel level) {
    return level == WalkLevel::kL1 ? BitRange{.low = 20, .width = 12}
                                   : BitRange{.low = 12, .width = 8};
}

/**
 * @brief         Entries one table holds at a level.
 * @param[in]     level   The level asked about.
 * @return        4096 at level 1, 256 at level 2.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr unsigned int EntriesAt(WalkLevel level) {
    return level == WalkLevel::kL1 ? 4096U : 256U;
}

/**
 * @brief         Reports whether a slot maps something.
 * @param[in]     entry   The table slot.
 * @return        true when either descriptor type bit is set.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr bool DescriptorPresent(const Entry& entry) {
    return (entry.raw & 0x3U) != 0;
}

/**
 * @brief         Reports whether a slot is a leaf at the given level.
 * @param[in]     entry   The table slot.
 * @param[in]     level   The level holding the slot.
 * @return        true for a section at level 1 or a page at level 2.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr bool DescriptorIsLeaf(const Entry& entry, WalkLevel level) {
    return level == WalkLevel::kL1 ? (entry.raw & 0x3U) == 0x2U : (entry.raw & 0x3U) != 0;
}

/**
 * @brief         Builds a pointer to a second-level table.
 * @param[in]     physical   Physical address of the table (1 KiB aligned).
 * @param[in]     flags      Ignored beyond presence.
 * @return        The encoded entry.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr Entry MakeTablePointer(unsigned long physical, Entry /*flags*/) {
    return Entry{static_cast<std::uint32_t>(physical & ~0x3FFUL) | 0x1U};
}

/**
 * @brief         Builds a small-page entry.
 * @param[in]     physical   Physical address of the page.
 * @param[in]     flags      kWritable / kUser / kNoExec, consumed here.
 * @return        The encoded entry.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr Entry MakePageEntry(unsigned long physical, Entry flags) {
    std::uint32_t terms = 0x2U;                      // small page
    terms |= (1U << 2U) | (1U << 3U) | (1U << 6U);   // B, C, TEX 001
    terms |= (flags.has(kUser) ? (0x3U << 4U) : (0x1U << 4U));  // AP: both worlds or PL1
    return Entry{static_cast<std::uint32_t>(physical & ~0xFFFUL) | terms};
}

/**
 * @brief         Reads the table a pointer entry names.
 * @param[in]     slot   A table-pointer entry.
 * @return        The second-level table's physical address.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr unsigned long TableTarget(const Entry& slot) {
    return slot.raw & 0xFFFFFC00UL;
}

/**
 * @brief         Reads the page a small-page entry names.
 * @param[in]     slot   A small-page entry.
 * @return        The page's physical address.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr unsigned long SmallLeafTarget(const Entry& slot) {
    return slot.raw & 0xFFFFF000UL;
}

/**
 * @brief         Reads the table index one level consumes.
 * @param[in]     virtual_address   The address being walked.
 * @param[in]     level             Which level's index to read.
 * @return        The index for that level.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long SlotIndex(unsigned long virtual_address, WalkLevel level) {
    return static_cast<unsigned long>(Entry{static_cast<std::uint32_t>(virtual_address)}.extract(
        IndexRange(level)));
}

/**
 * @brief         Keeps the low bits of an address below a leaf's base.
 * @param[in]     virtual_address   The address being resolved.
 * @param[in]     width             How many low bits the leaf offsets.
 * @return        The in-leaf offset.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long LowBits(unsigned long virtual_address, unsigned char width) {
    return static_cast<unsigned long>(
        Entry{static_cast<std::uint32_t>(virtual_address)}.extract(BitRange{.low = 0, .width = width}));
}

/**
 * @brief         Reports whether a level holds 4 KiB leaves.
 * @param[in]     level   The level asked about.
 * @return        true for level 2.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr bool IsLeafLevel(WalkLevel level) {
    return level == kLeafLevel;
}

/**
 * @brief         Reports whether a level may hold a large leaf.
 * @param[in]     level   The level asked about.
 * @return        true for level 1, where sections live.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr bool HasLargeLeaf(WalkLevel level) {
    return level == WalkLevel::kL1;
}

/**
 * @brief         Names the physical field of a level's large leaf.
 * @param[in]     level   The level whose field is wanted.
 * @return        Bits 31:20 for a section.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr BitRange LargeLeafPhys(WalkLevel /*level*/) {
    return BitRange{.low = 20, .width = 12};
}

/**
 * @brief         Reads the next-level table a pointer names.
 * @param[in]     slot   A table-pointer entry.
 * @return        The table's physical address.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long EntryTarget(const Entry& slot) {
    return TableTarget(slot);
}

/**
 * @brief         The frame a leaf entry names.
 * @param[in]     slot    A leaf entry.
 * @param[in]     level   The level that entry sits at.
 * @return        Physical base of the small page or section it names.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long LeafFrame(const Entry& slot, WalkLevel level) {
    return level == kLeafLevel ? SmallLeafTarget(slot) : (slot.raw & 0xFFF00000UL);
}

/// Slots of level 1 below the 3 GiB user boundary.
constexpr unsigned int kUserHalfSlots = 3072;

}  // namespace cinux::arch::page
