#include "kernel/arch/armv7/mmu.hpp"

#include <cstdint>

namespace cinux::armv7 {

namespace {

/// One section descriptor per MiB; the table must be 16 KiB aligned.
alignas(16384) std::uint32_t g_l1[4096];

constexpr std::uint32_t kSectionDescriptor = 0x2;
constexpr std::uint32_t kFullAccess        = 0x3U << 10;

/// Device window: B set, C clear, TEX 0, execute-never.
constexpr std::uint32_t kDeviceTerms = kSectionDescriptor | (1U << 2) | (1U << 4) | kFullAccess;

/// Normal memory: TEX 001, C set, B set, executable.
constexpr std::uint32_t kNormalTerms =
    kSectionDescriptor | (1U << 2) | (1U << 3) | (1U << 12) | kFullAccess;

void InvalidateTlb() {
    asm volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));  // TLBIALL
    asm volatile("dsb");
    asm volatile("isb");
}

}  // namespace

void MmuEnable() {
    for (std::uint32_t index = 0; index < 4096; ++index) {
        g_l1[index] = 0;
    }
    // 0x00000000..0x1FFFFFFF: the board's device windows (UART, GIC, ...).
    for (std::uint32_t index = 0; index < 512; ++index) {
        g_l1[index] = (index << 20U) | kDeviceTerms;
    }
    // 0x80000000..0x8FFFFFFF: the kernel image, stack, and BSS.
    for (std::uint32_t index = 2048; index < 2048U + 256U; ++index) {
        g_l1[index] = (index << 20U) | kNormalTerms;
    }

    // Domain 0 as client; every domain client keeps permission checks out
    // of the way for this first cut.
    asm volatile("mcr p15, 0, %0, c3, c0, 0" : : "r"(0x55555555U));  // DACR
    asm volatile("mcr p15, 0, %0, c2, c0, 2" : : "r"(0));            // TTBCR = 0
    asm volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"(static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(g_l1))));                    // TTBR0
    InvalidateTlb();

    std::uint32_t sctlr = 0;
    asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= 1U;  // M: the MMU walks the tables from here on.
    asm volatile("mcr p15, 0, %0, c1, c0, 0" : : "r"(sctlr));
    asm volatile("isb");
}

}  // namespace cinux::armv7
