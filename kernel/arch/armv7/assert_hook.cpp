/**
 * @file    assert_hook.cpp
 * @brief   The armv7 world's assertion hook: one line out the UART, then park.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include <source_location>

#include "cinux/assert.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/halt.hpp"

namespace cinux::base::safety {

[[noreturn]] void AssertionFailed(const char* k_message, std::source_location loc) {
    cinux::armv7::UartWrite("[panic] ");
    cinux::armv7::UartWrite(k_message);
    cinux::armv7::UartWrite(" at ");
    cinux::armv7::UartWrite(loc.file_name());
    cinux::armv7::UartWrite("\n");
    cinux::arch::Halt();
}

}  // namespace cinux::base::safety
