/**
 * @file    clock.hpp
 * @brief   x86-64's clock sources, gathered for the neutral platform face.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_platform
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/x86_64/platform/hpet/hpet.hpp"
#include "kernel/drivers/rtc/rtc.hpp"
#include "kernel/arch/x86_64/platform/pit/pit.hpp"

namespace cinux::platform {

/**
 * @brief         The finest monotonic nanoseconds this port can name.
 * @return        Nanoseconds since boot, or 0 when only the coarse
 *                tick remains — the caller falls back to the shared
 *                tick counter.
 * @since         0.1.0
 * @ingroup kernel_platform
 */
[[nodiscard]] inline unsigned long long FineClockNanoseconds() {
    const driver::Hpet& fine = driver::Hpet::self();
    return fine.present() ? fine.since_boot_ns() : 0ULL;
}

/// Wall seconds at boot from the battery-backed clock, read once.
[[nodiscard]] inline unsigned long long BootWallSeconds() {
    static const unsigned long long kRead = driver::Rtc::self().now_seconds();
    return kRead;
}

}  // namespace cinux::platform
