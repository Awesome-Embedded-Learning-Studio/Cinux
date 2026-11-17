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
    asm volatile("dsb sy" ::: "memory");
    asm volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"(physical) : "memory");
    asm volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0) : "memory");  // TLBIALL
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb" ::: "memory");
}

/// Flushes every translation this CPU holds.
inline void RefreshRoot() {
    asm volatile("dsb sy" ::: "memory");
    asm volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0) : "memory");
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb" ::: "memory");
}

/// Drops one page's translation.
/// Drops every translation; used after a table-wide change such as a fork.
inline void InvalidateAll() {
    asm volatile("dsb sy" ::: "memory");
    asm volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0) : "memory");
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb" ::: "memory");
}

inline void InvalidatePage(unsigned long virtual_address) {
    asm volatile("dsb sy" ::: "memory");
    asm volatile("mcr p15, 0, %0, c8, c7, 1" : : "r"(virtual_address) : "memory");  // TLBIMVA
    asm volatile("dsb sy" ::: "memory");
    asm volatile("isb" ::: "memory");
}

/// Reads the faulting address the last data abort named.
inline unsigned long FaultAddress() {
    unsigned long address = 0;
    asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(address));  // DFAR
    return address;
}

}  // namespace cinux::arch::space
