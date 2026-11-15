/**
 * @file    root_port.hpp
 * @brief    The arch half of the root volume port.
 *
 * The kernel_init ladder mounts the root volume through an erased
 * BlockPort plus a wall clock for the volume's stamps. Which device
 * sits behind the port -- SATA on x86, the card on armv7 -- is the
 * port's own word, so is the clock's reader. A lane that adopts its
 * own tree instead never asks for a port at all.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#ifndef CINUX_ARCH_ROOT_PORT_HPP_
#define CINUX_ARCH_ROOT_PORT_HPP_

namespace cinux::driver {
class BlockPort;
}  // namespace cinux::driver

namespace cinux::arch {

/// Builds the lane's root volume port and hands the wall clock reader
/// that goes with it. The port lives for the kernel's lifetime and is
/// never freed. The clock may stay null when the lane has no reader.
driver::BlockPort* MakeRootPort(unsigned long long (**wall_clock)());

}  // namespace cinux::arch

#endif  // CINUX_ARCH_ROOT_PORT_HPP_
