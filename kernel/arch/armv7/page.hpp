/**
 * @file    page.hpp
 * @brief   The page size of armv7: four kilobytes.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/literal_types.hpp"

namespace cinux::arch::page {

using cinux::base::operator""_KiB;

/// Page size of the short-descriptor MMU.
inline constexpr unsigned long kSize = 4_KiB;

}  // namespace cinux::arch::page
