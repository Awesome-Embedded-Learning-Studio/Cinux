/**
 * @file    irq.cpp
 * @brief   The armv7 IRQ handler, reached with a full user frame saved.
 *
 * Acknowledges the GIC, bumps the tick, and hands the same frame back:
 * this lane does not preempt yet, so the interrupted world resumes
 * exactly where it was.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.2
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/gic.hpp"
#include "kernel/arch/armv7/timer.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/boot/print.hpp"

namespace {

unsigned long g_ticks = 0;

void print_unsigned(unsigned long value) {
    char digits[24];
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

}  // namespace

void HandleIrq() {
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
