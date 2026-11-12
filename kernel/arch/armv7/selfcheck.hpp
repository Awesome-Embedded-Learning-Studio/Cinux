/**
 * @file    selfcheck.hpp
 * @brief   In-image capability checks for the armv7 lane.
 *
 * These are small, load-bearing assertions about kernel services, run
 * inside the image itself: no host framework, no x86 boot chain. The
 * first one covers the physical page manager.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::armv7 {

/// Allocates, touches, frees, and refuses an impossible order.
[[nodiscard]] bool RunPmmSelfCheck();

}  // namespace cinux::armv7
