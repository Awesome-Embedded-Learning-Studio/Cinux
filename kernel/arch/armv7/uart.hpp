/**
 * @file    uart.hpp
 * @brief   i.MX6ULL UART1 as a polling console, 115200 8N1.
 *
 * The i.MX UART lives at a fixed MMIO window and carries one quirk worth
 * knowing: its baud clock is not a register value but a chain. UART1's
 * root clock comes out of PLL3 at 80 MHz on reset; UFCR divides it by two
 * to 40 MHz; the bit clock is ref * (UBIR + 1) / (16 * (UBMR + 1)). With
 * UBIR = 15 and UBMR = 346 that is 115273 baud, 0.06% above 115200. QEMU
 * does not model the baud rate, so this is written for silicon.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

namespace cinux::armv7 {

/// UART1's MMIO base on the i.MX6ULL.
inline constexpr std::uintptr_t kUart1Base = 0x02020000U;

namespace uart_reg {
inline constexpr std::uint32_t kUrxd = 0x00;
inline constexpr std::uint32_t kUtxd = 0x40;
inline constexpr std::uint32_t kUcr1 = 0x80;
inline constexpr std::uint32_t kUcr2 = 0x84;
inline constexpr std::uint32_t kUcr3 = 0x88;
inline constexpr std::uint32_t kUcr4 = 0x8C;
inline constexpr std::uint32_t kUfcr = 0x90;
inline constexpr std::uint32_t kUsr2 = 0x98;
inline constexpr std::uint32_t kUbir = 0xA4;
inline constexpr std::uint32_t kUbmr = 0xA8;
}  // namespace uart_reg

/// Programs UART1 for 115200 8N1 with the transmitter and receiver on.
void UartInit();

/// Sends one byte, waiting for the transmitter to be ready.
void UartPutChar(char character);

/// Sends a NUL-terminated string.
void UartWrite(const char* text);

}  // namespace cinux::armv7
