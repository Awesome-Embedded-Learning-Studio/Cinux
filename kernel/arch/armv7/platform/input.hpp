/**
 * @file    input.hpp
 * @brief   The line's byte source, drained on demand.
 *
 * The port polls its UART from the tick and from the syscall door, but a
 * task parked in a wait loop makes neither: a line pasted while the desk
 * sleeps would overflow the receive FIFO and lose its tail. The neutral
 * wait loops call PumpInput for exactly that window.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_platform
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/armv7/platform/serial.hpp"

namespace cinux::platform {

/**
 * @brief         Drains the line into the console queue and event0.
 * @since         0.1.0
 * @ingroup       kernel_platform
 */
inline void PumpInput();

}  // namespace cinux::platform
