/**
 * @file    gic.hpp
 * @brief   The GIC-400 on the i.MX6ULL: distributor, CPU interface, and
 *          the one timestamp interrupt this bring-up listens to.
 *
 * The distributor owns which interrupts exist and who may see them; the
 * CPU interface owns the per-CPU handshake (acknowledge, priority mask,
 * end-of-interrupt). The generic timer's physical compare is PPI 30.
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

/// GIC distributor window on the i.MX6ULL.
inline constexpr std::uintptr_t kGicDistributorBase = 0x00A01000U;
/// GIC CPU interface window on the i.MX6ULL.
inline constexpr std::uintptr_t kGicCpuInterfaceBase = 0x00A02000U;
/// The generic timer's physical compare, as a PPI. A core that came up in
/// the secure state drives the secure physical timer (PPI 29); a non-secure
/// core drives PPI 30. Bring-up listens to both so the lane works either way.
inline constexpr std::uint32_t kTimerInterruptIdSecure    = 29;
inline constexpr std::uint32_t kTimerInterruptIdNonSecure = 30;

/// Turns the distributor and this CPU's interface on and lets the timer through.
void GicInit();

/// Lets one shared peripheral interrupt through at the timer's priority.
/// A device that grows an interrupt line calls this with its GIC id; the
/// dispatch in irq.cpp is the other half of wiring a device in.
void GicEnableInterrupt(std::uint32_t interrupt_id);

/// Reads and acknowledges the highest-priority pending interrupt.
[[nodiscard]] std::uint32_t GicAcknowledge();

/// Signals the end of one interrupt.
void GicEnd(std::uint32_t interrupt_id);

}  // namespace cinux::armv7
