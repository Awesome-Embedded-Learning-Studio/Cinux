/**
 * @file    space_layout.hpp
 * @brief   The armv7 address map: 32 bits, identity-mapped bring-up.
 *
 * There is no direct-map window on this port yet: the translation tables
 * map physical to virtual one to one, so the conversions are the identity.
 * The user half ends where the classic 3G/1G split puts it.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/literal_types.hpp"
#include "kernel/arch/armv7/page.hpp"

namespace cinux::arch::space {

using cinux::base::operator""_KiB;
using cinux::base::operator""_MiB;

/// The one first-level slot reserved for a task's own pages. The board's
/// kernel and device mappings live below it, so it is the only window a
/// new world must start empty.
constexpr bool IsUserSlot(unsigned long index) {
    return index == (0x40000000UL >> 20U);
}

/// The kernel heap window's virtual base: just past the managed RAM, so
/// the heap's virtual pages never collide with the identity map.
inline constexpr unsigned long kHeapBase = 0x90000000UL;

/// The heap window's reservation: second-level tables for this many
/// bytes are created once at bring-up and shared by every address space,
/// so runtime heap growth edits shared tables instead of installing a
/// private second level under whichever root happens to be live.
inline constexpr unsigned long kHeapReserveBytes = 16_MiB;

/// Highest user-visible address plus one: the classic 3 GiB split.
inline constexpr unsigned long kUserTop = 0xC0000000UL;

// The user-space seats for a 32-bit address space.
inline constexpr unsigned long kUserNullTop         = 64_KiB;
inline constexpr unsigned long kUserInterpreterBase = 0x10000000UL;  // 256 MiB
inline constexpr unsigned long kUserExecImageBase   = 0x20000000UL;  // 512 MiB
// The one-section user window (1 MiB at 0x40000000) seats the program
// low, the heap band next, and the mmap band above it.
inline constexpr unsigned long kUserBrkMmapSplit    = 0x40080000UL;
inline constexpr unsigned long kUserMmapTop         = 0x400F0000UL;
inline constexpr unsigned long kBrkSlideBytes       = 64_KiB;
inline constexpr unsigned long kMmapSlideBytes      = 128_KiB;
inline constexpr unsigned long kUserStackTop        = kUserTop - cinux::arch::page::kSize;
inline constexpr unsigned long kUserStackBytes      = 1_MiB;
inline constexpr unsigned long kUserStackBottom =
    kUserStackTop - kUserStackBytes - cinux::arch::page::kSize;

/**
 * @brief         Identity translation on this port.
 * @param[in]     physical   The physical address.
 * @return        The same address.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr unsigned long DirectMapVirt(unsigned long physical) {
    return physical;
}

/**
 * @brief         Identity translation on this port.
 * @param[in]     virtual_address   The address.
 * @return        The same address.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr unsigned long DirectMapPhys(unsigned long virtual_address) {
    return virtual_address;
}

/**
 * @brief         Every address is its own direct map entry here.
 * @return        true
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr bool IsDirectMap(unsigned long /*virtual_address*/) {
    return true;
}

/**
 * @brief         Reports whether the address is in the kernel half.
 * @param[in]     virtual_address   Address to classify.
 * @return        true from kUserTop up.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr bool IsKernelHalf(unsigned long virtual_address) {
    return virtual_address >= kUserTop;
}

/**
 * @brief         Device maps are the identity during bring-up.
 * @param[in]     physical   The device address.
 * @return        The same address.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr unsigned long IoremapVirt(unsigned long physical) {
    return physical;
}

}  // namespace cinux::arch::space
