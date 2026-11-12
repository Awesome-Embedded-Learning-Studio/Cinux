/**
 * @file    space_layout.hpp
 * @brief   The x86-64 virtual address map: PML4 slots, kernel-half windows,
 *          and the two fixed-offset translations built on them.
 *
 * Every constant here is a hardware fact of long mode: a PML4 slot spans
 * 2^39 bytes, the user half ends at the canonical boundary, and the direct
 * map mirrors physical RAM at one fixed offset. The memory subsystem owns
 * the *policy* on top of this map (how large the user stack is, where the
 * mmap area starts); this file owns only what the CPU forces. The
 * static_asserts pin each named window to the slot arithmetic it claims,
 * so the map and the page-table numbers cannot drift apart.
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
#include "kernel/arch/x86_64/page.hpp"

namespace cinux::arch::space {

using cinux::base::operator""_GiB;
using cinux::base::operator""_KiB;
using cinux::base::operator""_MiB;

/** @brief Bytes one PML4 entry spans; every reserved region is sized in whole entries. */
inline constexpr unsigned long kPageTableSpan = 512_GiB;

/**
 * @brief         Reports whether a top-level slot belongs to the user world.
 * @param[in]     index   PML4 slot.
 * @return        true for the lower 256 slots, which a fresh space starts empty.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr bool IsUserSlot(unsigned long index) {
    return index < 256;
}

/** @brief PML4 slot of the direct map of physical RAM. */
inline constexpr unsigned long kDirectMapSlot = 256;
/** @brief PML4 slot of the kernel heap window. */
inline constexpr unsigned long kHeapSlot      = 257;
/** @brief PML4 slot reserved for vmalloc and kernel thread stacks. */
inline constexpr unsigned long kVmallocSlot   = 258;
/** @brief PML4 slot reserved for ioremap and device MMIO. */
inline constexpr unsigned long kIoremapSlot   = 259;
/** @brief PML4 slot holding the kernel image and module windows. */
inline constexpr unsigned long kKernelSlot    = 511;

/** @brief Base of the direct map: virtual = base + physical, fixed offset. */
inline constexpr unsigned long kDirectMapBase = 0xFFFF800000000000UL;
/** @brief Base of the kernel heap window; grows upward inside its entry. */
inline constexpr unsigned long kHeapBase      = 0xFFFF808000000000UL;
/** @brief Base of the vmalloc reservation; no allocator serves it yet. */
inline constexpr unsigned long kVmallocBase   = 0xFFFF810000000000UL;
/** @brief Base of the ioremap reservation; phase one mirrors physical addresses. */
inline constexpr unsigned long kIoremapBase   = 0xFFFF818000000000UL;

/** @brief PDPT slot of the kernel image window inside the kernel PML4 entry. */
inline constexpr unsigned long kKernelImagePdpt = 510;
/** @brief PDPT slot of the module reservation inside the kernel PML4 entry. */
inline constexpr unsigned long kModulePdpt      = 511;

/** @brief Base of the kernel image window; the image itself still links at +2 MiB. */
inline constexpr unsigned long kKernelImageBase = 0xFFFFFFFF80000000UL;
/** @brief Base of the module reservation; the -mcmodel=kernel link keeps code in the last 2 GiB. */
inline constexpr unsigned long kModuleBase      = 0xFFFFFFFFC0000000UL;

/** @brief Highest user-visible address plus one; the CPU's canonical boundary. */
inline constexpr unsigned long kUserTop = 0x0000800000000000UL;

// The user-space seats this kernel promises. They are policy, but the
// policy is written against this CPU's user top and page size, so it
// travels with the port rather than with the memory subsystem.
inline constexpr unsigned long kUserNullTop         = 64_KiB;
inline constexpr unsigned long kUserInterpreterBase = 256_MiB;
inline constexpr unsigned long kUserExecImageBase   = 512_MiB;
inline constexpr unsigned long kUserBrkMmapSplit    = 0x0000400000000000UL;
inline constexpr unsigned long kUserMmapTop         = 0x00007F8000000000UL;
inline constexpr unsigned long kUserStackTop        = kUserTop - cinux::arch::page::kSize;
inline constexpr unsigned long kUserStackBytes      = 1_MiB;
inline constexpr unsigned long kUserStackBottom =
    kUserStackTop - kUserStackBytes - cinux::arch::page::kSize;

/**
 * @brief         Turns a physical address into its direct-map alias.
 * @param[in]     physical   Address inside the mapped RAM span.
 * @return        Virtual address of the same byte in the direct map.
 * @note          Meaningful only for physical addresses the direct map covers.
 * @warning       None
 * @throws        None
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr unsigned long DirectMapVirt(unsigned long physical) {
    return kDirectMapBase + physical;
}

/**
 * @brief         Recovers the physical address behind a direct-map pointer.
 * @param[in]     virtual_address   Address inside the direct-map entry.
 * @return        The physical byte the entry maps at that offset.
 * @note          Heap, vmalloc, and image addresses have no fixed offset;
 *                only the direct map converts by subtraction.
 * @warning       None
 * @throws        None
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr unsigned long DirectMapPhys(unsigned long virtual_address) {
    return virtual_address - kDirectMapBase;
}

/**
 * @brief         Reports whether the address falls in the direct map's slot.
 * @param[in]     virtual_address   Address to classify.
 * @return        true inside the direct map's whole 512 GiB span.
 * @note          Slot-wide membership; whether RAM is actually mapped there
 *                is the VMM's question, not the layout's.
 * @warning       None
 * @throws        None
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr bool IsDirectMap(unsigned long virtual_address) {
    return virtual_address >= kDirectMapBase && virtual_address < kDirectMapBase + kPageTableSpan;
}

/**
 * @brief         Reports whether the address lives in the kernel half.
 * @param[in]     virtual_address   Address to classify.
 * @return        true from the direct-map base up to the top of the space.
 * @warning       None
 * @throws        None
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr bool IsKernelHalf(unsigned long virtual_address) {
    return virtual_address >= kDirectMapBase;
}

/**
 * @brief         Turns a device physical address into its phase-one ioremap alias.
 * @param[in]     physical   MMIO address below the reservation's 512 GiB span.
 * @return        Virtual address inside the ioremap entry, same offset.
 * @note          Phase one mirrors physical offsets so early consumers such
 *                as the framebuffer need no allocator; a real ioremap that
 *                hands out tracked ranges replaces this when one is needed.
 * @warning       None
 * @throws        None
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr unsigned long IoremapVirt(unsigned long physical) {
    return kIoremapBase + physical;
}

static_assert(kPageTableSpan == (1UL << 39U), "one PML4 entry spans 2^39 bytes");
static_assert(kDirectMapBase == (0xFFFF000000000000UL | (kDirectMapSlot << 39U)),
              "direct map sits exactly at its PML4 slot");
static_assert(kHeapBase == kDirectMapBase + kPageTableSpan, "heap owns the next entry");
static_assert(kVmallocBase == kHeapBase + kPageTableSpan, "vmalloc owns the next entry");
static_assert(kIoremapBase == kVmallocBase + kPageTableSpan, "ioremap owns the next entry");
static_assert(kIoremapBase + kPageTableSpan <= 0xFFFFFF8000000000UL,
              "reserved entries stop before the kernel image entry");
static_assert(kKernelImageBase ==
                  (0xFFFF000000000000UL | (kKernelSlot << 39U) | (kKernelImagePdpt << 30U)),
              "image window sits exactly at its PDPT slot");
static_assert(kModuleBase == (0xFFFF000000000000UL | (kKernelSlot << 39U) | (kModulePdpt << 30U)),
              "module reservation sits exactly at its PDPT slot");
static_assert(kModuleBase == kKernelImageBase + 1_GiB, "modules follow the image window");
static_assert(kUserTop == (1UL << 47U), "user half ends at the canonical boundary");

}  // namespace cinux::arch::space
