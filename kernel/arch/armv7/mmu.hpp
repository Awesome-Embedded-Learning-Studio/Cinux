/**
 * @file    mmu.hpp
 * @brief   The armv7 short-descriptor MMU, first cut: identity sections.
 *
 * An L1 table of 4096 section descriptors covers the 4 GiB space. This
 * bring-up maps the low 512 MiB (UART, GIC, the board's device windows)
 * as device memory and the kernel's 256 MiB at 0x80000000 as normal
 * write-back memory, then turns the MMU on. Two-level tables, cache
 * policies per region, and user mappings come with the real port.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::armv7 {

/// Builds the section table and turns the MMU on.
void MmuEnable();

}  // namespace cinux::armv7
