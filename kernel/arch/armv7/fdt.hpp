/**
 * @file    fdt.hpp
 * @brief   A small flattened-device-tree reader, enough for the memory node.
 *
 * The blob's header points at a structure block (tokens and nodes) and a
 * strings block (property names). This reader walks the structure once,
 * picks the node whose device_type says "memory", and decodes its reg
 * property as a 32-bit base and size, which is the shape the QEMU board
 * hands out. A full parser (phandles, cells, interrupts) comes with the
 * real port.
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

namespace cinux::armv7 {

/// One memory range the firmware described.
struct MemoryRange {
    std::uint32_t base = 0;
    std::uint32_t size = 0;
};

/**
 * @brief         Reads the first /memory reg from a device tree blob.
 * @param[in]     blob   Physical address of the blob.
 * @param[out]    range  Receives base and size.
 * @return        true when the blob is well formed and has a memory node.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
[[nodiscard]] bool ReadMemoryRange(std::uint32_t blob, MemoryRange& range);

}  // namespace cinux::armv7
