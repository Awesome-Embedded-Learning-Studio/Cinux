/**
 * @file    page_walk_shape.hpp
 * @brief   The x86-64 page-table grammar: table width, the four levels,
 *          and the bit ranges each level reads.
 *
 * The walk algorithm lives in the memory subsystem and knows nothing about
 * how many levels this machine has; it iterates the shape declared here.
 * Everything in this file is a hardware fact of long mode: four levels of
 * 512 entries, nine index bits per level, and a physical-address field
 * starting at bit 12.
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
#include "kernel/arch/x86_64/page_entry.hpp"

namespace cinux::arch::page {

using cinux::base::bit::BitRange;

/// Entries one table page holds; a hardware fact, not a tuning knob.
constexpr unsigned int kEntriesPerTable = 512;

/// Slots of one user half: the lower half of the top level a user world may own.
constexpr unsigned int kUserHalfSlots = kEntriesPerTable / 2;

/**
 * @brief   One step of the four-level descent, named.
 * @note    kPml4 reads the topmost nine index bits, kPageTable the
 *          lowest; large-page leaves may sit at kPdpt (1 GiB pages)
 *          and kPageDirectory (2 MiB pages).
 * @since   0.2.0
 * @ingroup kernel_arch
 */
enum class WalkLevel : unsigned char {
    kPml4          = 0,
    kPdpt          = 1,
    kPageDirectory = 2,
    kPageTable     = 3,
};

/// The level a 4 KiB leaf occupies; every walk ends here or at a large leaf above it.
constexpr WalkLevel kLeafLevel = WalkLevel::kPageTable;

/**
 * @brief         Where one level reads its nine-bit index in an address.
 *
 * @param[in]     level   The walk level asked about.
 * @return        Bit range of that level's index.
 * @note          A large leaf's in-page offset spans exactly the low
 *                bits below its own index range, so the range's low
 *                doubles as the leaf's offset width.
 * @since         0.2.0
 * @ingroup       kernel_arch
 */
constexpr BitRange IndexRange(WalkLevel level) {
    switch (level) {
    case WalkLevel::kPml4:
        return {.low = 39, .width = 9};
    case WalkLevel::kPdpt:
        return {.low = 30, .width = 9};
    case WalkLevel::kPageDirectory:
        return {.low = 21, .width = 9};
    case WalkLevel::kPageTable:
        return {.low = 12, .width = 9};
    }
    return {.low = 0, .width = 0};
}

/// The level a 4 KiB leaf occupies; every walk ends here or at a large leaf above it.
// (kLeafLevel already declared above.)

/**
 * @brief         Reports whether a level holds 4 KiB leaves.
 * @param[in]     level   The level asked about.
 * @return        true for the last level of the descent.
 * @since         0.3.0
 * @ingroup       kernel_arch
 */
constexpr bool IsLeafLevel(WalkLevel level) {
    return level == kLeafLevel;
}


/// Reports whether a leaf is a large-page mapping: a 2 MiB leaf stands
/// where a 4 KiB leaf would, and a permission rewrite must not mistake
/// it for one.
constexpr bool IsLargeLeaf(const Entry& leaf) {
    return leaf.has(kLarge);
}

/// Reports whether a leaf carries the copy-on-write mark: the frame is
/// shared and a write must copy before it lands.
constexpr bool LeafIsShareMarked(const Entry& leaf) {
    return leaf.has(kCow);
}

/// Stamps the copy-on-write mark onto freshly built terms.
constexpr Entry ShareMarked(Entry terms) {
    return terms | kCow;
}

/// Reports whether a slot maps something. On this port the present bit
/// is the grammar, so the predicate reads it directly.
constexpr bool IsPresent(const Entry& entry) {
    return entry.has(kPresent);
}

/// Reports whether an upper-level slot is a plain table pointer rather
/// than a large-page leaf.
constexpr bool IsTableEntry(const Entry& entry, WalkLevel /*level*/) {
    return !entry.has(kLarge);
}

/// Reads a leaf's permission terms back in encoder-flag form. The x86
/// leaf bits are the encoder bits, so this just drops the software
/// copy-on-write mark: the child maps fresh and MarkPageShared re-marks.
constexpr Entry LeafTermsOf(const Entry& leaf) {
    return leaf & ~kCow;
}

/**
 * @brief         Reports whether a level may hold a large leaf.
 * @param[in]     level   The level asked about.
 * @return        true where this machine allows a large page.
 * @note          x86-64 allows them at the PDPT (1 GiB) and the page
 *                directory (2 MiB), never at the top level.
 * @since         0.3.0
 * @ingroup       kernel_arch
 */
constexpr bool HasLargeLeaf(WalkLevel level) {
    return level == WalkLevel::kPdpt || level == WalkLevel::kPageDirectory;
}

/**
 * @brief         Names the physical-address field of a level's large leaf.
 * @param[in]     level   The level whose field is wanted.
 * @return        Bit range of that level's physical address.
 * @since         0.3.0
 * @ingroup       kernel_arch
 */
constexpr BitRange LargeLeafPhys(WalkLevel level) {
    return level == WalkLevel::kPdpt ? kHugePagePhys : kLargePagePhys;
}

/// The levels the walk descends through before the leaf table.
constexpr WalkLevel kUpperLevels[3] = {
    WalkLevel::kPml4,
    WalkLevel::kPdpt,
    WalkLevel::kPageDirectory,
};

/**
 * @brief         Reads the table index one level consumes.
 *
 * @param[in]     virtual_address   The address being walked.
 * @param[in]     level             Which level's index to read.
 * @return        The nine-bit index for that level.
 * @since         0.2.0
 * @ingroup       kernel_arch
 */
inline unsigned long SlotIndex(unsigned long virtual_address, WalkLevel level) {
    return static_cast<unsigned long>(Entry{virtual_address}.extract(IndexRange(level)));
}

/**
 * @brief         Keeps the low bits of an address below a leaf's base.
 *
 * @param[in]     virtual_address   The address being resolved.
 * @param[in]     width             How many low bits the leaf offsets.
 * @return        The in-leaf offset.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long LowBits(unsigned long virtual_address, unsigned char width) {
    return static_cast<unsigned long>(
        Entry{virtual_address}.extract(BitRange{.low = 0, .width = width}));
}

/**
 * @brief         Reads the physical address a table-pointer entry names.
 *
 * @param[in]     slot   A table-pointer entry.
 * @return        The next table's physical address.
 * @since         0.2.0
 * @ingroup       kernel_arch
 */
inline unsigned long EntryTarget(const Entry& slot) {
    Entry address{};
    address.deposit(kTablePhys, slot.extract(kTablePhys));
    return address.raw;
}

/**
 * @brief         The frame a leaf entry names.
 * @param[in]     slot    A leaf entry.
 * @param[in]     level   The level that entry sits at.
 * @return        Physical address of the frame.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long LeafFrame(const Entry& slot, WalkLevel /*level*/) {
    return EntryTarget(slot);
}

/**
 * @brief         Reports whether a slot maps something.
 * @param[in]     entry   The table slot.
 * @return        true when the present bit is set.
 * @since         0.4.0
 * @ingroup       kernel_arch
 */
constexpr bool DescriptorPresent(const Entry& entry) {
    return entry.has(kPresent);
}

/**
 * @brief         Reports whether a slot is a leaf at the given level.
 * @param[in]     entry   The table slot.
 * @param[in]     level   The level holding the slot.
 * @return        true when the large bit marks a leaf.
 * @since         0.4.0
 * @ingroup       kernel_arch
 */
constexpr bool DescriptorIsLeaf(const Entry& entry, WalkLevel /*level*/) {
    return entry.has(kLarge);
}

/**
 * @brief         Builds a pointer entry to the next-level table.
 * @param[in]     physical   Physical address of the next table.
 * @param[in]     flags      Extra terms.
 * @return        The encoded entry.
 * @since         0.4.0
 * @ingroup       kernel_arch
 */
constexpr Entry MakeTablePointer(unsigned long physical, Entry flags) {
    return MakeTableEntry(physical, flags);
}

/**
 * @brief         Builds a 4 KiB leaf entry.
 * @param[in]     physical   Physical address of the page.
 * @param[in]     flags      Permission terms.
 * @return        The encoded entry.
 * @since         0.4.0
 * @ingroup       kernel_arch
 */
constexpr Entry MakePageEntry(unsigned long physical, Entry flags) {
    return MakeTableEntry(physical, flags);
}

/**
 * @brief         Entries one table holds at a level.
 * @param[in]     level   The level asked about.
 * @return        512 at every level on x86-64.
 * @since         0.4.0
 * @ingroup       kernel_arch
 */
constexpr unsigned int EntriesAt(WalkLevel /*level*/) {
    return kEntriesPerTable;
}

/**
 * @brief         Reads the physical address a table pointer names.
 * @param[in]     slot   A table-pointer entry.
 * @return        The next table's physical address.
 * @since         0.4.0
 * @ingroup       kernel_arch
 */
constexpr unsigned long TableTarget(const Entry& slot) {
    return EntryTarget(slot);
}

/**
 * @brief         Reads the physical address a 4 KiB leaf names.
 * @param[in]     slot   A small-page entry.
 * @return        The page's physical address.
 * @since         0.4.0
 * @ingroup       kernel_arch
 */
constexpr unsigned long SmallLeafTarget(const Entry& slot) {
    return EntryTarget(slot);
}
/**
 * @brief         The frame a small leaf names, in one neutral name.
 * @param[in]     leaf   A 4 KiB leaf entry.
 * @return        Physical base of the frame.
 * @since         0.3.0
 * @ingroup       base_page
 */
constexpr unsigned long LeafPhysical(const Entry& leaf) {
    return SmallLeafTarget(leaf);
}


}  // namespace cinux::arch::page
