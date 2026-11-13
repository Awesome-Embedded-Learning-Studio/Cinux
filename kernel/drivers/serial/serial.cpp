/**
 * @file    serial.cpp
 * @brief   The Serial face: a forwarding shell over the port's serial.
 *
 * The register grammar lives in each port's platform serial header;
 * this file only keeps the neutral names the drivers include stable.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.2.0
 * @since   0.1.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#include "kernel/drivers/serial/serial.hpp"

#include "cinux/platform/serial.hpp"

namespace cinux::driver {

void SerialInit() {
    cinux::platform::SerialInit();
}

void SerialPutChar(char character) {
    cinux::platform::SerialPutChar(character);
}

void SerialDrain() {
    cinux::platform::SerialDrain();
}

bool SerialTryGetChar(char* out) {
    return cinux::platform::SerialTryGetChar(out);
}

bool SerialHasKey() {
    return cinux::platform::SerialHasKey();
}

}  // namespace cinux::driver
