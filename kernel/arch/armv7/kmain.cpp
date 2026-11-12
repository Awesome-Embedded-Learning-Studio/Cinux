/**
 * @file    kmain.cpp
 * @brief   The armv7 bring-up entry: say hello, show the timer, wait.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.2
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/fdt.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/arch/armv7/gic.hpp"
#include "kernel/arch/armv7/mmu.hpp"
#include "kernel/arch/armv7/timer.hpp"
#include "kernel/arch/armv7/uart.hpp"

namespace {

void PrintUnsigned(unsigned long long value) {
    char digits[21];
    int  count = 0;
    if (value == 0) {
        cinux::armv7::UartPutChar('0');
        return;
    }
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

void PrintHex(unsigned long long value) {
    constexpr char kDigits[] = "0123456789ABCDEF";
    bool           started   = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const unsigned nibble = static_cast<unsigned>((value >> shift) & 0xFU);
        if (nibble != 0 || started || shift == 0) {
            cinux::armv7::UartPutChar(kDigits[nibble]);
            started = true;
        }
    }
}

}  // namespace

extern "C" [[noreturn]] void kmain() {
    cinux::armv7::UartInit();
    cinux::armv7::UartWrite("\nCinux armv7: alive on i.MX6ULL\n");

    cinux::armv7::UartWrite("[armv7] page ");
    PrintUnsigned(cinux::arch::page::kSize);
    cinux::armv7::UartWrite(" bytes, timer: freq ");
    PrintUnsigned(cinux::armv7::TimerFrequency());
    cinux::armv7::UartWrite(" Hz, counter 0x");
    PrintHex(cinux::armv7::TimerCounter());
    cinux::armv7::UartWrite("\n");

    extern int _vectors[];
    asm volatile("mcr p15, 0, %0, c12, c0, 0" : : "r"(_vectors));  // VBAR
    asm volatile("isb");
    cinux::armv7::GicInit();
    cinux::armv7::TimerArmAfter(cinux::armv7::TimerFrequency() / 10U);
    cinux::armv7::TimerEnable();
    cinux::armv7::MmuEnable();
    cinux::armv7::UartWrite("[armv7] mmu on\n");
    cinux::armv7::UartWrite(cinux::armv7::TwoLevelDemo() ? "[armv7] two-level page ok\n"
                                                        : "[armv7] two-level page FAILED\n");
    {
        constexpr unsigned long kDtbAddress = 0x80100000UL;
        cinux::armv7::MemoryRange range{};
        if (cinux::armv7::ReadMemoryRange(kDtbAddress, range)) {
            cinux::armv7::UartWrite("[armv7] dtb memory 0x");
            PrintHex(range.base);
            cinux::armv7::UartWrite(" + 0x");
            PrintHex(range.size);
            cinux::armv7::UartWrite("\n");
        } else {
            cinux::armv7::UartWrite("[armv7] dtb missing or unreadable, first word 0x");
            PrintHex(*reinterpret_cast<volatile unsigned long*>(kDtbAddress));
            cinux::armv7::UartWrite("\n");
        }
    }
    asm volatile("cpsie if" ::: "memory");
    cinux::armv7::UartWrite("[armv7] irq on, timer armed\n");
    for (;;) {
        asm volatile("wfi");
    }
}
