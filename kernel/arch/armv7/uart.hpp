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

/// UART1's line at the GIC: SPI 26, so GIC id 58. The device tree names
/// the raw SPI number; the GIC counts sixteen software interrupts and
/// sixteen per-processor ones ahead of the shared bank.
inline constexpr std::uint32_t kUart1InterruptId = 58;

namespace uart_reg {
inline constexpr std::uint32_t kUrxd = 0x00;
inline constexpr std::uint32_t kUtxd = 0x40;
inline constexpr std::uint32_t kUcr1 = 0x80;
inline constexpr std::uint32_t kUcr2 = 0x84;
inline constexpr std::uint32_t kUcr3 = 0x88;
inline constexpr std::uint32_t kUcr4 = 0x8C;
inline constexpr std::uint32_t kUfcr = 0x90;
inline constexpr std::uint32_t kUsr2 = 0x98;
inline constexpr std::uint32_t kUts1 = 0xB4;
/// USR2 bit 0: a byte is waiting in URXD.
inline constexpr std::uint32_t kUsr2Rdr = 1U << 0U;
/// UTS1 bit 5: the receive FIFO is empty.
inline constexpr std::uint32_t kUts1RxEmpty = 1U << 5U;
/// UTS1 bit 6: the whole transmit path, FIFO and shifter, is empty.
inline constexpr std::uint32_t kUts1TxEmpty = 1U << 6U;
inline constexpr std::uint32_t kUbir = 0xA4;
inline constexpr std::uint32_t kUbmr = 0xA8;
}  // namespace uart_reg

/// Programs UART1 for 115200 8N1 with the transmitter and receiver on.
void UartInit();

/// Sends one byte, waiting for the transmitter to be ready.
void UartPutChar(char character);

/// Waits until the transmitter has fully emptied, transmit FIFO and
/// shift register both, so the last byte has left the machine.
void UartDrain();

/// Sends a NUL-terminated string.
void UartWrite(const char* text);

/// Reports whether a received byte is waiting in URXD.
[[nodiscard]] bool UartReceiveReady();

/// Takes the waiting byte; only call after UartReceiveReady.
[[nodiscard]] char UartReadChar();

/// Raises UART1's receive line at the GIC. From here every byte that
/// lands raises the interrupt, so a paste cannot outwait the poll.
void UartEnableRxInterrupt();

}  // namespace cinux::armv7
