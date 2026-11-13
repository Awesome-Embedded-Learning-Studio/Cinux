/**
 * @file    serial.hpp
 * @brief   The serial port behind the neutral Serial face, armv7.
 *
 * Here that port is the i.MX6ULL's UART1, the same line the boot
 * console speaks, so the Serial face is a thin forward to the uart
 * driver that already owns the registers.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_platform
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/armv7/uart.hpp"

namespace cinux::platform {

/// The uart driver's idempotent bring-up already ran for the console.
inline void SerialInit() {
    cinux::armv7::UartInit();
}

/// One byte out, waiting until the line can take it.
inline void SerialPutChar(char character) {
    cinux::armv7::UartPutChar(character);
}

/// Waits until the transmitter has fully emptied.
inline void SerialDrain() {
    cinux::armv7::UartDrain();
}

/// Polls one byte in, false when the line is quiet.
inline bool SerialTryGetChar(char* out) {
    if (!cinux::armv7::UartReceiveReady()) {
        return false;
    }
    *out = cinux::armv7::UartReadChar();
    return true;
}

/// Whether a byte waits behind the receive-ready bit.
inline bool SerialHasKey() {
    return cinux::armv7::UartReceiveReady();
}

}  // namespace cinux::platform
