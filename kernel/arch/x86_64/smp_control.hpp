/**
 * @file    smp_control.hpp
 * @brief   The x86-64 side of cross-processor wakeups and CPU identity.
 *
 * The scheduler only ever wants one thing from another CPU: wake it up.
 * How that wakeup travels (an APIC IPI here) is the port's business, as
 * is the number the hardware stamps on each processor.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/x86_64/smp.hpp"

namespace cinux::arch {

/**
 * @brief         Wakes the other processors so they re-check dispatch.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline void NotifyRunnable() {
    smp::NotifyRunnable();
}

/**
 * @brief         Reports how many processors are online.
 * @return        The online count, always at least one.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
[[nodiscard]] inline unsigned int OnlineCpuCount() {
    return smp::OnlineCpuCount();
}

/**
 * @brief         Reads the hardware identity of one online processor.
 * @param[in]     ordinal   Zero-based index into the online set.
 * @return        The port's hardware id for that processor (APIC id here).
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
[[nodiscard]] inline unsigned int CpuHardwareId(unsigned int ordinal) {
    return smp::Mechanisms(ordinal).apic_id;
}

}  // namespace cinux::arch
