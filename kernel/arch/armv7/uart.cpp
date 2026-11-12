#include "kernel/arch/armv7/uart.hpp"

namespace cinux::armv7 {

namespace {

constexpr std::uint32_t kUcr1Uarten = 1U << 0;
constexpr std::uint32_t kUcr2Srst   = 1U << 0;
constexpr std::uint32_t kUcr2Rxen   = 1U << 1;
constexpr std::uint32_t kUcr2Txen   = 1U << 2;
constexpr std::uint32_t kUcr2Ws     = 1U << 5;  // 1 selects 8 data bits.
constexpr std::uint32_t kUcr2Irts   = 1U << 14;
constexpr std::uint32_t kUcr3Dsr    = 1U << 10;  // Data set ready, as U-Boot sets it.
constexpr std::uint32_t kUcr3RxdMuxSel = 1U << 2;  // The manual says always set.
constexpr std::uint32_t kUsr2Txdc      = 1U << 3;
constexpr std::uint32_t kUfcrRfdivDiv2 = 4U << 7;
constexpr std::uint32_t kUbirValue     = 15;
constexpr std::uint32_t kUbmrValue     = 346;

std::uint32_t ReadRegister(std::uint32_t offset) {
    return *reinterpret_cast<volatile std::uint32_t*>(kUart1Base + offset);
}

void WriteRegister(std::uint32_t offset, std::uint32_t value) {
    *reinterpret_cast<volatile std::uint32_t*>(kUart1Base + offset) = value;
}

/// Gives the controller a moment after the soft reset before reprogramming.
void WaitForReset() {
    for (std::uint32_t spin = 0; spin < 1000; spin = spin + 1) {
        asm volatile("" ::: "memory");
    }
}

}  // namespace

void UartInit() {
    WriteRegister(uart_reg::kUcr1, 0);
    // SRST self-clears on silicon; QEMU's model keeps it set, so the reset
    // is not polled: the final UCR2 write settles both models.
    WriteRegister(uart_reg::kUcr2, kUcr2Srst);
    WaitForReset();

    WriteRegister(uart_reg::kUcr3, kUcr3Dsr | kUcr3RxdMuxSel);
    WriteRegister(uart_reg::kUcr4, 0);
    WriteRegister(uart_reg::kUfcr, kUfcrRfdivDiv2);
    WriteRegister(uart_reg::kUbir, kUbirValue);
    WriteRegister(uart_reg::kUbmr, kUbmrValue);

    // 8 data bits, ignore RTS, transmitter and receiver on.
    WriteRegister(uart_reg::kUcr2, kUcr2Ws | kUcr2Irts | kUcr2Txen | kUcr2Rxen);
    WriteRegister(uart_reg::kUcr1, kUcr1Uarten);
}

void UartPutChar(char character) {
    while ((ReadRegister(uart_reg::kUsr2) & kUsr2Txdc) == 0) {
    }
    WriteRegister(uart_reg::kUtxd,
                  static_cast<std::uint32_t>(static_cast<unsigned char>(character)));
}

void UartWrite(const char* text) {
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        UartPutChar(*cursor);
    }
}

}  // namespace cinux::armv7
