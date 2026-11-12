#include "kernel/arch/armv7/gic.hpp"
#include "kernel/arch/armv7/timer.hpp"
#include "kernel/arch/armv7/uart.hpp"

namespace {

volatile unsigned long long g_ticks = 0;

void PrintUnsigned(unsigned long long value) {
    char digits[21];
    int  count = 0;
    while (value != 0) {
        digits[count] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
        ++count;
    }
    while (count > 0) {
        --count;
        cinux::armv7::UartPutChar(digits[count]);
    }
}

}  // namespace

/// C side of the IRQ vector: acknowledge, serve, end.
extern "C" void c_irq_handler() {
    const unsigned long id = cinux::armv7::GicAcknowledge();
    if (id == cinux::armv7::kTimerInterruptIdSecure ||
        id == cinux::armv7::kTimerInterruptIdNonSecure) {
        ++g_ticks;
        cinux::armv7::TimerArmAfter(cinux::armv7::TimerFrequency() / 10U);
        cinux::armv7::UartWrite("[armv7] tick ");
        PrintUnsigned(g_ticks);
        cinux::armv7::UartWrite("\n");
    }
    cinux::armv7::GicEnd(static_cast<unsigned long>(id));
}
