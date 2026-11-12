/**
 * @file    irq_guard.hpp
 * @brief   The armv7 interrupt-flag discipline as a ScopedGuard policy.
 *
 * CPSR's I bit is the whole state: entering snapshots it and masks IRQ,
 * exiting restores the snapshot so a nested guard never reopens the gate
 * its caller wanted shut.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/scoped_guard.hpp"

namespace cinux::arch {

/**
 * @brief         Snapshots CPSR and masks IRQ.
 * @return        The snapshot for RestoreIrq.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long long SaveAndDisableIrq() {
    unsigned long long cpsr = 0;
    asm volatile("mrs %0, cpsr" : "=r"(cpsr));
    asm volatile("cpsid i" ::: "memory");
    return cpsr;
}

/**
 * @brief         Puts the interrupt state back as the snapshot found it.
 * @param[in]     snapshot   What SaveAndDisableIrq handed out.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline void RestoreIrq(unsigned long long snapshot) {
    asm volatile("msr cpsr_c, %0" : : "r"(static_cast<unsigned long>(snapshot)) : "memory");
}

/// Save-and-mask policy for the ScopedGuard shell.
struct IrqLockPolicy {
    using State = unsigned long long;

    static void enter(State& state) { state = SaveAndDisableIrq(); }

    static void exit(State& state) { RestoreIrq(state); }
};

/// Interrupts saved on entry, restored on scope exit; nests safely.
using IrqGuard = cinux::base::ScopedGuard<IrqLockPolicy>;

}  // namespace cinux::arch
