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
#include "kernel/time/tick.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/uart_input.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/syscall/sys_clock.hpp"

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

void serve_timer() {
    ++g_ticks;
    // The neutral heartbeat counts too: the clock every timeout is
    // measured against lives in Tick, and this port used never to feed
    // it, so a 15 ms poll waited forever. This timer runs slower than
    // the neutral rate, so one interrupt credits the ticks it covers.
    constexpr unsigned long kTicksPerInterrupt = cinux::time::kTickHz.value / 10UL;
    for (unsigned long credit = 0; credit < kTicksPerInterrupt; ++credit) {
        cinux::time::Tick::self().on_interrupt();
    }
    if ((g_ticks % 2) == 1) {
        cinux::armv7::UartWrite("[armv7] tick ");
        print_unsigned(g_ticks);
        cinux::armv7::UartWrite("\n");
    }
    // One interrupt per neutral tick: the same rate x86's PIT keeps, so
    // a second is a second on both lanes.
    cinux::armv7::TimerArmAfter(cinux::armv7::TimerFrequency() / 10U);
}


/// One interrupt entry: acknowledges whatever the GIC hands over and
/// serves it. The timer is no longer the only source — UART1's receive
/// line rides the same door — so the id decides the service, and the
/// loop runs until the GIC answers "spurious". The bound keeps a
/// steady paste from parking the CPU in one entry forever: the line is
/// level-triggered, so whatever is left re-raises it at once.
void serve_interrupt() {
    constexpr unsigned long kSpurious = 1023;
    for (unsigned round = 0; round < 8; ++round) {
        const unsigned long interrupt = cinux::armv7::GicAcknowledge();
        if (interrupt == kSpurious) {
            return;
        }
        cinux::armv7::GicEnd(interrupt);
        const bool kTimer = interrupt == cinux::armv7::kTimerInterruptIdSecure ||
                            interrupt == cinux::armv7::kTimerInterruptIdNonSecure;
        if (kTimer) {
            serve_timer();
        } else if (interrupt == cinux::armv7::kUart1InterruptId) {
            // A paste cannot outwait the poll any more: every byte that
            // lands raises this line, so the drain runs as the bytes
            // arrive instead of once per heartbeat.
            cinux::armv7::PollSerialKeyboard();
        }
    }
}

}  // namespace

/// An interrupt taken in the kernel: tick and return through the stub.
void HandleIrq() {
    serve_interrupt();
}

/// The seated door the vector stub reads before any C runs. The user
/// image is still in the registers at that point, so the stub may only
/// load this word - a call would eat the user's r0-r3, lr. The seat
/// world raises it for its episodes and drops it on the way out.
extern "C" unsigned long c_seat_irq_door = 0;

/// The seated IRQ service: the frame is homed on the interrupted
/// task's own supervisor stack, so the tick may switch this task away
/// mid-frame and the switch saves a stack the task owns. The neutral
/// scheduler gets the same tick the x86 timer hands it.
extern "C" cinux::arch::UserTrap* c_irq_trap_seated(cinux::arch::UserTrap* trap) {
    serve_interrupt();
    cinux::proc::Scheduler::self().on_timer_tick(cinux::syscall::MonotonicNanoseconds());
    cinux::proc::Scheduler::self().maybe_preempt();
    return trap;
}

/// A user-mode interrupt that reached the legacy door: with the
/// bring-up task world gone, every user episode is seated and takes
/// the homing door, so this only answers the tick and resumes.
extern "C" cinux::arch::UserTrap* c_irq_trap(cinux::arch::UserTrap* trap) {
    serve_interrupt();
    return trap;
}
