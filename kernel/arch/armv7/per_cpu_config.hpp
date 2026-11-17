/**
 * @file    per_cpu_config.hpp
 * @brief   Per-CPU sizing for the armv7 lane.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::arch::per_cpu {

/// Processors the block table can hold; the i.MX6ULL has one.
inline constexpr unsigned int kMaxCpus = 4;

}  // namespace cinux::arch::per_cpu
