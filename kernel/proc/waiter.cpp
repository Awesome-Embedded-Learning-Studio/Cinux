/**
 * @file    waiter.cpp
 * @brief   The waiting turn: pump input, open interrupts, yield.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.2.0
 * @since   0.1.0
 * @ingroup kernel_proc
 * @copyright Copyright (c) 2026
 */

#include "kernel/proc/waiter.hpp"

#include "cinux/platform/input.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/proc/scheduler.hpp"

namespace cinux::proc {

void WaitForNextTurn() {
    // The port drains its input first: a paste longer than the FIFO loses
    // its tail while the desk sleeps, and a wait is exactly when nobody
    // else is looking at the line.
    platform::PumpInput();
    // The exception that brought us here masked interrupts, so the timer
    // would never tick and a clock-based deadline would never arrive: the
    // gate opens for the span of the yield and closes again exactly as the
    // caller had it.
    const unsigned long long kGate = arch::SaveAndEnableIrq();
    Scheduler::self().yield();
    arch::RestoreIrq(kGate);
}

}  // namespace cinux::proc
