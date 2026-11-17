/**
 * @file    clock.hpp
 * @brief   armv7's clock source for the neutral platform face.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_platform
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/armv7/timer.hpp"

namespace cinux::platform {

/**
 * @brief         The finest monotonic nanoseconds this port can name.
 * @return        Always 0 — the GPT counter does not yet publish
 *                nanoseconds, so the shared tick counter answers.
 * @since         0.1.0
 * @ingroup kernel_platform
 */
[[nodiscard]] inline unsigned long long FineClockNanoseconds() {
    return 0ULL;
}

/// No battery-backed clock on this board; the epoch is the boot.
[[nodiscard]] inline unsigned long long BootWallSeconds() {
    return 0ULL;
}

}  // namespace cinux::platform
