/**
 * @file    device_tree.hpp
 * @brief   The neutral device-tree assembly face.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_fs
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/drivers/input/event_stream.hpp"
#include "kernel/fs/devfs/devfs.hpp"

namespace cinux::fs {

/**
 * @brief         Assembles the shared device tree onto one devfs.
 * @param[in]     devfs          The devfs the device model binds to.
 * @param[in]     primary_input  The keyboard-side event source.
 * @param[in]     pointer_input  The pointer-side event source.
 * @note          Runs the neutral order — model bind, builtin,
 *                console, framebuffer, pty, inputs — so both ports
 *                grow the same /dev tree. Only the two event sources
 *                differ between ports.
 * @since         0.1.0
 * @ingroup       kernel_fs
 */
void SeatDeviceTree(DevFs& devfs, driver::EventStream& primary_input,
                    driver::EventStream& pointer_input);



}  // namespace cinux::fs
