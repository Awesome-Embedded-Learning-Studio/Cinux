/**
 * @file    smp_control.hpp
 * @brief   The armv7 side of cross-processor wakeups and CPU identity.
 *
 * One core on this board: waking the others is a no-op, and the online
 * count is one. The names exist so shared scheduler code compiles.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::arch {

/// Nothing to wake on a single-core board.
inline void NotifyRunnable() {}

/// The one processor this lane runs on.
[[nodiscard]] inline unsigned int OnlineCpuCount() {
    return 1;
}

}  // namespace cinux::arch
