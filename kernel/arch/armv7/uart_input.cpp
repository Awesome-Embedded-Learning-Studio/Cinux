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

#include "kernel/drivers/input/event_stream.hpp"
#include "kernel/drivers/input/input_config.hpp"

namespace cinux::armv7 {

namespace {

driver::EventStream g_keyboard_stream;
driver::EventStream g_pointer_stream;


}  // namespace

driver::EventStream& UartKeyboardEvents() {
    return g_keyboard_stream;
}

driver::EventStream& BoardPointerEvents() {
    return g_pointer_stream;
}

}  // namespace cinux::armv7
