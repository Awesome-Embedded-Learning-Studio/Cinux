/**
 * @file    input.hpp
 * @brief   The line's byte source, drained on demand.
 *
 * This port's console reader already pumps the line, so a parked wait
 * has nothing extra to drain.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_platform
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::platform {

/**
 * @brief         Nothing to drain: the console reader owns the line.
 * @since         0.1.0
 * @ingroup       kernel_platform
 */
inline void PumpInput() {}

}  // namespace cinux::platform
