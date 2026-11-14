/**
 * @file    uart_input.cpp
 * @brief   UART bytes in, key events out: event0's honest source.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/platform/serial.hpp"
#include "kernel/drivers/tty/console_tty.hpp"
#include "kernel/drivers/usb/usb_keyboard.hpp"
#include "kernel/arch/armv7/uart_input.hpp"

#include <cstdint>

#include "kernel/drivers/input/event_stream.hpp"
#include "kernel/drivers/input/input_config.hpp"

namespace cinux::armv7 {

namespace {

driver::EventStream g_keyboard_stream;
driver::EventStream g_pointer_stream;


}  // namespace

namespace {

/// The serial line's ASCII bytes, in the evdev codes a keymap expects.
/// Unshifted letters and digits plus the editing keys are enough for a
/// shell; anything else is swallowed rather than guessed.
uint16_t evdev_code_for(char character) {
    if (character >= 'a' && character <= 'z') {
        return static_cast<uint16_t>(30 + (character - 'a'));
    }
    if (character >= 'A' && character <= 'Z') {
        return static_cast<uint16_t>(30 + (character - 'A'));
    }
    if (character >= '1' && character <= '9') {
        return static_cast<uint16_t>(2 + (character - '1'));
    }
    switch (character) {
        case '0': return 11;
        case '\r':
        case '\n': return 28;   // KEY_ENTER
        case '\t': return 15;   // KEY_TAB
        case 0x7F:
        case '\b': return 14;   // KEY_BACKSPACE
        case ' ': return 57;     // KEY_SPACE
        case '-': return 12;
        case '=': return 13;
        case '.': return 52;
        case ',': return 51;
        case '/': return 53;
        default: return 0;
    }
}

}  // namespace

void PollSerialKeyboard() {
    for (unsigned long budget = 0; budget < 8; ++budget) {
        char character = 0;
        if (!cinux::platform::SerialTryGetChar(&character) &&
            !cinux::driver::UsbKeyboard::self().take(&character)) {
            return;
        }
        // The console queue still gets the byte: whichever consumer
        // asked first cannot starve the other, because both are fed
        // here, at the source.
        static_cast<void>(cinux::driver::ConsoleTty().feed(character));
        const uint16_t kCode = evdev_code_for(character);
        if (kCode == 0) {
            continue;
        }
        g_keyboard_stream.publish(driver::kEvKey, kCode, 1);
        g_keyboard_stream.publish(driver::kEvKey, kCode, 0);
        g_keyboard_stream.publish_report();
    }
}

driver::EventStream& UartKeyboardEvents() {
    return g_keyboard_stream;
}

driver::EventStream& BoardPointerEvents() {
    return g_pointer_stream;
}

}  // namespace cinux::armv7
