/**
 * @file    space.hpp
 * @brief   The address-space facts of the architecture this tree is built for.
 *
 * One forwarding header names the current port's space layout, so consumers
 * include "kernel/arch/space.hpp" and never spell an ISA directory. A new
 * port adds its own layout header and repoints the include below; nothing
 * else in the tree moves.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/port/space_layout.hpp"

namespace cinux::arch::space {

/// The port this build targets; named here so downstream code can report it.
inline constexpr const char* kPortName = "x86_64";

}  // namespace cinux::arch::space
