/**
 * @file    space_control.hpp
 * @brief   The armv7 controls over the live address space: TTBR0, TLB.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::arch::space {

/// Reports the translation table base in use.
inline unsigned long ActiveRoot() {
    unsigned long root = 0;
    asm volatile("mrc p15, 0, %0, c2, c0, 0" : "=r"(root));
    return root;
}

/// Points the MMU at another first-level table.
inline void SwitchRoot(unsigned long physical) {
    asm volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"(physical));
    asm volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));  // TLBIALL
    asm volatile("isb");
}

/// Flushes every translation this CPU holds.
inline void RefreshRoot() {
    asm volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));
    asm volatile("dsb");
    asm volatile("isb");
}

/// Drops one page's translation.
inline void InvalidatePage(unsigned long virtual_address) {
    asm volatile("mcr p15, 0, %0, c8, c7, 1" : : "r"(virtual_address));  // TLBIMVA
    asm volatile("dsb");
    asm volatile("isb");
}

/// Reads the faulting address the last data abort named.
inline unsigned long FaultAddress() {
    unsigned long address = 0;
    asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(address));  // DFAR
    return address;
}

}  // namespace cinux::arch::space
