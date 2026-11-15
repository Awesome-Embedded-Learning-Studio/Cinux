/**
 * @file    console_world.hpp
 * @brief    The arch half of the file world's console seats.
 *
 * The file world grows the mounts -- root, /dev, /proc -- itself; the
 * seats that answer which hardware a machine actually has are the
 * port's own words. One door, two spellings: the x86 lane taps its
 * PS/2 and USB keyboards into the console and seats its SATA and NVMe
 * rows in the device model, the arm lane taps its UART and USB
 * keyboards and seats the card.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#ifndef CINUX_ARCH_CONSOLE_WORLD_HPP_
#define CINUX_ARCH_CONSOLE_WORLD_HPP_

#include "kernel/fs/devfs/devfs.hpp"

namespace cinux::arch {

/// Grows the lane's console world inside the file world's device tree:
/// the console key tap, the input event seats, the USB keyboard node,
/// the disk model rows. Called once right after the file world's
/// mounts; the tree itself is shared and never rebuilt here.
void SeatConsoleWorld(fs::DevFs& devices);

}  // namespace cinux::arch

#endif  // CINUX_ARCH_CONSOLE_WORLD_HPP_
