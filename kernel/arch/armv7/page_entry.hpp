/**
 * @file    page_entry.hpp
 * @brief   The armv7 short-descriptor grammar: 32-bit entries in two shapes.
 *
 * A first-level entry is either a 1 MiB section (bits[1:0] = 0b10) or a
 * pointer to a second-level table (0b01). A second-level entry is a small
 * page (0b10) or a large page (0b01). The walk asks the predicates below
 * instead of reading bits itself, because the two architectures disagree
 * on what "present" and "leaf" look like.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "cinux/bit_ops/bitmask.hpp"

namespace cinux::arch::page {

/// One page-table entry: 32 bits on this port.
using Entry = base::bit::BitMask<std::uint32_t>;

/// Encoder flag: the mapping may be written through. Consumed by the
/// encoders below; it is not a descriptor bit position.
inline constexpr Entry kWritable = base::bit::MaskBit<std::uint32_t>(30);

/// Encoder flag: ring 3 may reach the mapping. Consumed by the encoders.
inline constexpr Entry kUser = base::bit::MaskBit<std::uint32_t>(29);

/// Encoder flag: instruction fetches refuse. Consumed by the encoders.
inline constexpr Entry kNoExec = base::bit::MaskBit<std::uint32_t>(28);

/// Encoder flag: user mode may read but not write. Consumed by the encoders.
inline constexpr Entry kReadOnly = base::bit::MaskBit<std::uint32_t>(27);

/// The two short-descriptor levels.
enum class WalkLevel : unsigned char {
    kL1 = 0,
    kL2 = 1,
};

}  // namespace cinux::arch::page
