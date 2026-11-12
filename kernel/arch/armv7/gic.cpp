#include "kernel/arch/armv7/gic.hpp"

namespace cinux::armv7 {

namespace {

constexpr std::uint32_t kGicdCtlr      = 0x000;
constexpr std::uint32_t kGicdIgroup    = 0x080;
constexpr std::uint32_t kGicdIsenabler = 0x100;
constexpr std::uint32_t kGicdIpriority = 0x400;
constexpr std::uint32_t kGiccCtlr      = 0x000;
constexpr std::uint32_t kGiccPmr       = 0x004;
constexpr std::uint32_t kGiccIar       = 0x00C;
constexpr std::uint32_t kGiccEoir      = 0x010;

std::uint32_t Read(std::uintptr_t base, std::uint32_t offset) {
    return *reinterpret_cast<volatile std::uint32_t*>(base + offset);
}

void Write(std::uintptr_t base, std::uint32_t offset, std::uint32_t value) {
    *reinterpret_cast<volatile std::uint32_t*>(base + offset) = value;
}

}  // namespace

void GicInit() {
    // Distributor: enable group 0/1 forwarding, then let PPI 30 through.
    Write(kGicDistributorBase, kGicdCtlr, 1);
    Write(kGicDistributorBase, kGicdIpriority + (kTimerInterruptIdSecure & ~3U),
          0xA0A0A0A0U);
    Write(kGicDistributorBase, kGicdIsenabler,
          (1U << kTimerInterruptIdSecure) | (1U << kTimerInterruptIdNonSecure));

    // CPU interface: priority mask wide open, then both groups enabled.
    Write(kGicCpuInterfaceBase, kGiccPmr, 0xFF);
    Write(kGicCpuInterfaceBase, kGiccCtlr, 0x3);
    asm volatile("isb");
}

std::uint32_t GicAcknowledge() {
    return Read(kGicCpuInterfaceBase, kGiccIar) & 0x3FFU;
}

void GicEnd(std::uint32_t interrupt_id) {
    Write(kGicCpuInterfaceBase, kGiccEoir, interrupt_id);
}

}  // namespace cinux::armv7
