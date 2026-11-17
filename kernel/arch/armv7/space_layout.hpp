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

/// The task's own window: every first-level slot from 1 GiB up to the
/// heap reservation's base belongs to the world, and the kernel half
/// below 1 GiB is the identity map's. A new world starts with these
/// slots empty and fills them on demand, one private second level at a
/// time.
constexpr bool IsUserSlot(unsigned long index) {
    return index >= (0x40000000UL >> 20U) && index < (0x80000000UL >> 20U);
}

/// The kernel heap window's virtual base: just past the managed RAM, so
/// the heap's virtual pages never collide with the identity map.
inline constexpr unsigned long kHeapBase = 0x90000000UL;

/// The heap window's reservation: second-level tables for this many
/// bytes are created once at bring-up and shared by every address space,
/// so runtime heap growth edits shared tables instead of installing a
/// private second level under whichever root happens to be live.
inline constexpr unsigned long kHeapReserveBytes = 16_MiB;

/// Highest user-visible address plus one: the kernel's own identity
// map starts here, so a user mapping can never shadow the kernel image.
inline constexpr unsigned long kUserTop = 0x80000000UL;

// The user-space seats for a 32-bit address space.
inline constexpr unsigned long kUserNullTop         = 64_KiB;
inline constexpr unsigned long kUserInterpreterBase = 0x50000000UL;  // 1.25 GiB
inline constexpr unsigned long kUserExecImageBase   = 0x40000000UL;  // the window's foot
// The window seats the program and its break low, the mmap band above,
// and the entry stack at the very top, under the heap reservation.
inline constexpr unsigned long kUserBrkMmapSplit    = 0x60000000UL;
inline constexpr unsigned long kUserMmapTop         = 0x7FEDE000UL;
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
