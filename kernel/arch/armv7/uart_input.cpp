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

#include "kernel/arch/armv7/uart_input.hpp"

#include <cstdint>

#include "kernel/arch/armv7/uart.hpp"
#include "kernel/drivers/input/event_stream.hpp"
#include "kernel/drivers/input/input_config.hpp"

namespace cinux::armv7 {

namespace {

driver::EventStream g_keyboard_stream;
driver::EventStream g_pointer_stream;

/// Linux keycodes for printable ASCII, zero where none matches. The
/// letters follow the standard map (KEY_A 30, KEY_B 48, ...), not the
/// alphabet's order, because that is the map user programs read.
constexpr uint16_t kAsciiToKeycode[128] = {
    0,  0,  0,  0,  0,  0,  0,  0,  // 0x00-0x07
    0,  0,  0,  0,  0,  0,  0,  0,  // 0x08-0x0F
    0,  0,  0,  0,  0,  0,  0,  0,  // 0x10-0x17
    0,  0,  28, 0,  0,  0,  0,  0,  // 0x18-0x1F: Enter
    57, 2,  3,  4,  5,  6,  7,  8,  // 0x20-0x27: Space, 1..7
    9,  10, 0,  0,  0,  0,  0,  11, // 0x28-0x2F: 8, 9, 0
    0,  30, 48, 46, 32, 18, 33, 34, // 0x30-0x37: A B C D E F G
    35, 23, 36, 37, 38, 50, 49, 24, // 0x38-0x3F: H I J K L M N O
    25, 16, 19, 31, 20, 22, 47, 17, // 0x40-0x47: P Q R S T U V W
    45, 21, 44, 0,  0,  0,  0,  0,  // 0x48-0x4F: X Y Z
    0,  0,  0,  0,  0,  0,  0,  0,  // 0x50-0x57
    0,  0,  0,  0,  0,  0,  0,  0,  // 0x58-0x5F
    0,  30, 48, 46, 32, 18, 33, 34, // 0x60-0x67: a b c d e f g
    35, 23, 36, 37, 38, 50, 49, 24, // 0x68-0x6F: h i j k l m n o
    25, 16, 19, 31, 20, 22, 47, 17, // 0x70-0x77: p q r s t u v w
    45, 21, 44, 0,  0,  0,  0,  0,  // 0x78-0x7F: x y z
};

}  // namespace

driver::EventStream& UartKeyboardEvents() {
    return g_keyboard_stream;
}

driver::EventStream& BoardPointerEvents() {
    return g_pointer_stream;
}

void PollUartKeyboard() {
    bool published = false;
    while (UartReceiveReady()) {
        const unsigned char kByte = static_cast<unsigned char>(UartReadChar());
        if (kByte >= 128) {
            continue;
        }
        const uint16_t kCode = kAsciiToKeycode[kByte];
        if (kCode == 0) {
            continue;
        }
        g_keyboard_stream.publish(driver::kEvKey, kCode, 1);
        g_keyboard_stream.publish(driver::kEvKey, kCode, 0);
        published = true;
    }
    if (published) {
        g_keyboard_stream.publish_report();
    }
}

}  // namespace cinux::armv7
