/**
 * @file    irq.cpp
 * @brief   The armv7 IRQ service: acknowledge, tick, re-arm, maybe preempt.
 *
 * Two entries share the service. An interrupt that landed in user mode
 * arrives with a full trap frame and may spend a time slice; one that
 * landed in the kernel has no frame to move and only gets the tick.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.3
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/gic.hpp"
#include "kernel/arch/armv7/per_cpu.hpp"
#include "kernel/arch/armv7/syscall_numbers.hpp"
#include "kernel/arch/armv7/timer.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/user_trap.hpp"

namespace {

unsigned long g_ticks = 0;

void print_unsigned(unsigned long value) {
    char          digits[24];
    unsigned long count = 0;
    if (value == 0) {
        cinux::armv7::UartPutChar('0');
        return;
    }
    while (value != 0) {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }
    while (count != 0) {
        cinux::armv7::UartPutChar(digits[--count]);
    }
}

void serve_tick() {
    const unsigned long interrupt = cinux::armv7::GicAcknowledge();
    cinux::armv7::GicEnd(interrupt);
    ++g_ticks;
    if ((g_ticks % 2) == 1) {
        cinux::armv7::UartWrite("[armv7] tick ");
        print_unsigned(g_ticks);
        cinux::armv7::UartWrite("\n");
    }
    cinux::armv7::TimerArmAfter(cinux::armv7::TimerFrequency() / 10U);
}

}  // namespace

extern "C" bool SchedulerRunning();
extern "C" cinux::arch::UserTrap* c_preempt_from_irq(cinux::arch::UserTrap* trap);

/// An interrupt taken in the kernel: tick and return through the stub.
void HandleIrq() {
    serve_tick();
}

/// An interrupt taken in user mode: the trap may be exchanged for another.
extern "C" cinux::arch::UserTrap* c_irq_trap(cinux::arch::UserTrap* trap) {
    serve_tick();
    if (!SchedulerRunning()) {
        return trap;
    }
    unsigned long& slice = cinux::arch::per_cpu::self().slice_ticks;
    if (slice != 0) {
        --slice;
        if (slice != 0) {
            return trap;
        }
    }
    return c_preempt_from_irq(trap);
}
