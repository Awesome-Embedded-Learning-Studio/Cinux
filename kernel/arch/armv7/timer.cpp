#include "kernel/arch/armv7/timer.hpp"

namespace cinux::armv7 {

namespace {

constexpr std::uint32_t kCntpCtlEnable = 1U << 0;
constexpr std::uint32_t kCntpCtlImask  = 1U << 1;

// CP15 c14,c2 opc2 selects the physical timer register: 0 is TVAL, 1 is
// CTL, 2 is CVAL.
constexpr std::uint32_t kCntpTval = 0;
constexpr std::uint32_t kCntpCtl  = 1;
constexpr std::uint32_t kCntpCval = 2;

std::uint32_t ReadTimerRegister(std::uint32_t index) {
    std::uint32_t value = 0;
    asm volatile("mrc p15, 0, %0, c14, c2, %1" : "=r"(value) : "I"(index));
    return value;
}

void WriteTimerRegister(std::uint32_t index, std::uint32_t value) {
    asm volatile("mcr p15, 0, %0, c14, c2, %1" : : "r"(value), "I"(index));
}

}  // namespace

std::uint32_t TimerFrequency() {
    std::uint32_t frequency = 0;
    asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(frequency));
    return frequency;
}

std::uint64_t TimerCounter() {
    std::uint32_t low  = 0;
    std::uint32_t high = 0;
    asm volatile("mrrc p15, 0, %0, %1, c14" : "=r"(low), "=r"(high));
    return (static_cast<std::uint64_t>(high) << 32U) | low;
}

void TimerArmAfter(std::uint32_t ticks) {
    WriteTimerRegister(kCntpTval, ticks);
}

void TimerEnable() {
    WriteTimerRegister(kCntpCtl, kCntpCtlEnable);
    asm volatile("isb");
}

}  // namespace cinux::armv7
