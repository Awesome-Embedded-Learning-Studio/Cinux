/**
 * @file    serial.hpp
 * @brief   The serial port behind the neutral Serial face, x86-64.
 *
 * Here that port is COM1, an 8250-UART on the legacy I/O ports,
 * programmed for polling: 115200 8N1, FIFOs on, interrupts off. The
 * bytes leave through the same OutB every other driver uses.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_platform
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "cinux/platform/io.hpp"

namespace cinux::platform {

namespace serial_detail {

constexpr uint16_t kCom1Base = 0x3F8;

constexpr uint8_t kRegData = 0x0;
constexpr uint8_t kRegIer  = 0x1;
constexpr uint8_t kRegFcr  = 0x2;
constexpr uint8_t kRegLcr  = 0x3;
constexpr uint8_t kRegMcr  = 0x4;
constexpr uint8_t kRegLsr  = 0x5;

constexpr uint8_t kLsrThre = 0x20;
constexpr uint8_t kLsrTemt = 0x40;
constexpr uint8_t kLsrDr   = 0x01;

/// The 8250 comes up speaking whatever the bootloader left; this
/// sequence retires that state: interrupts off, DLAB-latched divisor
/// for 115200, eight data bits, one stop, no parity, FIFOs on and
/// cleared, and DTR+RTS+OUT2 so the line is driven.
constexpr driver::PortWrite kInitSequence[] = {
    {.port = kCom1Base + kRegIer, .value = 0x00},  {.port = kCom1Base + kRegLcr, .value = 0x80},
    {.port = kCom1Base + kRegData, .value = 0x01}, {.port = kCom1Base + kRegIer, .value = 0x00},
    {.port = kCom1Base + kRegLcr, .value = 0x03},  {.port = kCom1Base + kRegFcr, .value = 0xC7},
    {.port = kCom1Base + kRegMcr, .value = 0x0B},
};

/// Polls one status register until every given bit reads back set.
inline void WaitBitsSet(uint16_t port, uint8_t bits) {
    while ((driver::InB(port) & bits) != bits) {
    }
}

}  // namespace serial_detail

/// Idempotent bring-up of the polling port.
inline void SerialInit() {
    for (const driver::PortWrite& write : serial_detail::kInitSequence) {
        driver::OutB(write);
    }
}

/// One byte out, waiting until the line can take it.
inline void SerialPutChar(char character) {
    serial_detail::WaitBitsSet(serial_detail::kCom1Base + serial_detail::kRegLsr,
                               serial_detail::kLsrThre);
    driver::OutB({.port = serial_detail::kCom1Base + serial_detail::kRegData,
          .value = static_cast<uint8_t>(character)});
}

/// Waits until the transmitter has fully emptied.
inline void SerialDrain() {
    serial_detail::WaitBitsSet(serial_detail::kCom1Base + serial_detail::kRegLsr,
                               serial_detail::kLsrTemt);
}

/// Polls one byte in, false when the line is quiet.
inline bool SerialTryGetChar(char* out) {
    if ((driver::InB(serial_detail::kCom1Base + serial_detail::kRegLsr) & serial_detail::kLsrDr) == 0) {
        return false;
    }
    *out = static_cast<char>(driver::InB(serial_detail::kCom1Base + serial_detail::kRegData));
    return true;
}

/// Whether a byte waits behind the data-ready bit.
inline bool SerialHasKey() {
    return (driver::InB(serial_detail::kCom1Base + serial_detail::kRegLsr) & serial_detail::kLsrDr) != 0;
}

}  // namespace cinux::platform
