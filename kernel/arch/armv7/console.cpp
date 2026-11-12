/**
 * @file    console.cpp
 * @brief   The armv7 console backend: everything goes out UART1.
 *
 * The kernel's Print/Println funnel into console::PutString; on x86 that
 * lands on the 16550 and the framebuffer console. The armv7 lane has no
 * 16550 and no framebuffer yet, so this file provides the same three
 * console symbols over the i.MX UART.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/uart.hpp"
#include "kernel/boot/boot_info.hpp"
#include "kernel/console/console.hpp"

namespace cinux::console {

void InitConsole(const cinux::boot::BootInfo& info) {
    static_cast<void>(info);
    cinux::armv7::UartInit();
}

void PutChar(char character) {
    cinux::armv7::UartPutChar(character);
}

void PutString(const char* text, bool newline) {
    while (*text != '\0') {
        cinux::armv7::UartPutChar(*text);
        ++text;
    }
    if (newline) {
        cinux::armv7::UartPutChar('\n');
    }
}

}  // namespace cinux::console
