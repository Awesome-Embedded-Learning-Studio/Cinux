/**
 * @file    fdt.hpp
 * @brief   The flattened device tree reader this port boots with.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.2.0
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

/// A borrowed view of one property's bytes inside the blob.
struct PropertyBytes {
    const std::uint8_t* bytes = nullptr;
    std::uint32_t      len    = 0;
};

[[nodiscard]] bool ReadMemoryRange(std::uint32_t blob, MemoryRange& range);

/// Finds one property by absolute node path, e.g. node_path
/// "/opt/cinux" with property "init-env". The view dies with the blob.
[[nodiscard]] bool ReadProperty(std::uint32_t blob, const char* node_path,
                                const char* property, PropertyBytes& out);

}  // namespace cinux::armv7
