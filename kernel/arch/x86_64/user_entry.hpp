/**
 * @file    user_entry.hpp
 * @brief   The x86-64 handoff from the kernel into a user program.
 *
 * One action: leave ring 0 for the given entry and stack and never come
 * back. How the machine drops privilege (an IRETQ frame here) is the
 * port's business.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/x86_64/usermode.hpp"

namespace cinux::arch {

/// Fresh user RFLAGS, with interrupts enabled and the reserved bit set.
constexpr unsigned long long UserEntryFlags(unsigned long long) {
    return 0x202ULL;
}

/**
 * @brief         Leaves the kernel for a user entry point.
 * @param[in]     entry   User virtual address of the first instruction.
 * @param[in]     stack   User stack pointer to hand over.
 * @note          Never returns; the user program either runs or the
 *                machine faults.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
[[noreturn]] inline void EnterUserMode(unsigned long long entry, unsigned long long stack) {
    JumpToRing3(entry, stack);
}

}  // namespace cinux::arch
