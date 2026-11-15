/**
 * @file    platform_name.hpp
 * @brief   The x86_64 word an AT_PLATFORM consumer reads.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::arch {

/// The string an AT_PLATFORM consumer reads on this port.
inline constexpr char kPlatformName[] = "x86_64";

}  // namespace cinux::arch
