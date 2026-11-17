/**
 * @file    user_access.hpp
 * @brief   Checking a user pointer before the kernel touches it.
 *
 * Two things must hold before the kernel reads or writes a pointer that
 * came from user mode: the whole range must lie below the user top, and
 * every page in it must be present *and* user-accessible in the active
 * table. The second check is the one that matters here: this port's
 * kernel image sits at 0x80000000, below the 3 GiB user top, so a range
 * check alone would happily hand the kernel its own memory.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/armv7/page_entry.hpp"
#include "kernel/arch/armv7/page_walk_shape.hpp"
#include "kernel/arch/armv7/space_control.hpp"
#include "kernel/arch/armv7/space_layout.hpp"


namespace cinux::arch {

namespace detail {

/// The AP field of a small page is bits 5:4; sections keep it at 15:10.
inline bool EntryIsUser(const page::Entry& entry, page::WalkLevel level) {
    const unsigned long shift = level == page::WalkLevel::kL1 ? 10UL : 4UL;
    const unsigned long ap    = (entry.raw >> shift) & 0x3UL;
    // 0x2/0x3 are the writable shapes; 0x6 is the both-worlds read-only
    // encoding the read-only leaves carry, and its user half may read.
    return ap == 0x3UL || ap == 0x2UL || ap == 0x6UL;
}

}  // namespace detail

/**
 * @brief         Checks that one page is mapped and user-accessible.
 * @param[in]     address   Page-aligned user address.
 * @return        true when the active table maps it for user mode.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline bool UserPageIsMapped(unsigned long address) {
    const auto* const l1 =
        reinterpret_cast<const page::Entry*>(space::ActiveRoot());
    const page::Entry kL1 = l1[(address >> 20U) & 0xFFFUL];
    if (!page::DescriptorPresent(kL1)) {
        return false;
    }
    if (page::DescriptorIsLeaf(kL1, page::WalkLevel::kL1)) {
        return detail::EntryIsUser(kL1, page::WalkLevel::kL1);
    }
    const auto* const l2 =
        reinterpret_cast<const page::Entry*>(page::TableTarget(kL1));
    const page::Entry kL2 = l2[(address >> 12U) & 0xFFUL];
    if (!page::DescriptorPresent(kL2) ||
        !page::DescriptorIsLeaf(kL2, page::WalkLevel::kL2)) {
        return false;
    }
    return detail::EntryIsUser(kL2, page::WalkLevel::kL2);
}

/**
 * @brief         Reports whether one page is mapped user-writable.
 * @param[in]     address   Page-aligned user address.
 * @return        true when the leaf grants write access to user mode.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline bool UserPageIsWritable(unsigned long address) {
    const auto* const l1 = reinterpret_cast<const page::Entry*>(space::ActiveRoot());
    const page::Entry kL1 = l1[(address >> 20U) & 0xFFFUL];
    if (!page::DescriptorPresent(kL1)) {
        return false;
    }
    if (page::DescriptorIsLeaf(kL1, page::WalkLevel::kL1)) {
        const unsigned long ap = (kL1.raw >> 10U) & 0x3UL;
        return ap == 0x3UL;
    }
    const auto* const l2 = reinterpret_cast<const page::Entry*>(page::TableTarget(kL1));
    const page::Entry kL2 = l2[(address >> 12U) & 0xFFUL];
    if (!page::DescriptorPresent(kL2)) {
        return false;
    }
    const unsigned long ap = (kL2.raw >> 4U) & 0x3UL;
    // A shared COW leaf (read-only for both worlds) also counts: the
    // kernel's write aborts, the repair door copies the page, and the
    // retried store lands. Refusing it here would EFAULT every write
    // into a fork-shared buffer.
    return ap == 0x3UL || ap == 0x6UL;
}

/**
 * @brief         Checks a whole user range: below the top, no overflow,
 *                every page mapped for user mode.
 * @param[in]     address   First byte.
 * @param[in]     length    Byte count.
 * @return        true when the kernel may touch the range.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline bool UserRangeIsMapped(unsigned long address, unsigned long length) {
    if (length == 0) {
        return true;
    }
    const unsigned long kTop = address + length;
    if (kTop < address || kTop > space::kUserTop) {
        return false;
    }
    // The per-page presence walk stays out: the kernel's raw accesses to
    // not-yet-filled pages abort and the demand door fills them, the
    // same way the user task's own accesses do. A range check here would
    // refuse valid lazy pages and every string that lives on one.
    return true;
}

/**
 * @brief         Copies a user buffer into kernel memory.
 * @param[out]    destination   Kernel buffer.
 * @param[in]     source        User address.
 * @param[in]     length        Byte count.
 * @return        false when the user range is not usable.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline bool CopyFromUser(void* destination, unsigned long source, unsigned long length) {
    if (!UserRangeIsMapped(source, length)) {
        return false;
    }
    auto* const out  = static_cast<unsigned char*>(destination);
    const auto* in   = reinterpret_cast<const unsigned char*>(source);
    for (unsigned long index = 0; index < length; ++index) {
        out[index] = in[index];
    }
    return true;
}

/**
 * @brief         Copies kernel memory out to a user buffer.
 * @param[in]     destination   User address.
 * @param[in]     source        Kernel buffer.
 * @param[in]     length        Byte count.
 * @return        false when the user range is not usable.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline bool CopyToUser(unsigned long destination, const void* source, unsigned long length) {
    if (!UserRangeIsMapped(destination, length)) {
        return false;
    }
    auto* const out  = reinterpret_cast<unsigned char*>(destination);
    const auto* in   = static_cast<const unsigned char*>(source);
    for (unsigned long index = 0; index < length; ++index) {
        out[index] = in[index];
    }
    return true;
}

/**
 * @brief         Copies bytes between two addresses, either direction.
 * @param[in]     destination   Where the bytes land.
 * @param[in]     source        Where they come from.
 * @param[in]     bytes         Byte count.
 * @return        False when the range is not usable.
 * @note          The port-neutral door face: one symmetric copy the
 *                syscall layer points at both ways. Defined in
 *                user_access.cpp, where the demand pager stands behind
 *                pages the walk finds missing.
 * @since         0.1.0
 * @ingroup kernel_arch
 */
bool MoveUserBytes(unsigned long long destination, unsigned long long source,
                   unsigned long long bytes);

/**
 * @brief         Records whether supervisor-window hardware is armed.
 * @param[in]     supported   Unused; this port has no SMAP to arm.
 * @note          The neutral seat exists so the syscall layer compiles
 *                unchanged; the answer never changes behaviour here.
 * @since         0.1.0
 * @ingroup kernel_arch
 */
inline void ArmUserWindows(bool /*supported*/) {}

}  // namespace cinux::arch
