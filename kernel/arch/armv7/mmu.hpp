/**
 * @file    mmu.hpp
 * @brief   The armv7 short-descriptor MMU: section map plus one second-level
 *          table, enough to prove two-level translation.
 *
 * The L1 table maps the board's device windows and the kernel's RAM as
 * 1 MiB sections, and carries one page-table descriptor for a demo window
 * at 0x40000000. TwoLevelDemo() writes through that window and checks the
 * bytes landed in the backing page, which is how this lane proves the
 * second level actually translates.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.2
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::armv7 {

/// Builds the tables and turns the MMU on.
void MmuEnable();

/// Writes through the two-level window and reports whether it landed.
[[nodiscard]] bool TwoLevelDemo();

}  // namespace cinux::armv7
