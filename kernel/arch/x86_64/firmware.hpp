/**
 * @file    firmware.hpp
 * @brief   The x86-64 firmware-provided strings: ACPI tables and the
 *          boot payload's parameter store.
 *
 * One action: read a named string the firmware left for the kernel.
 * Which store answers (ACPI, a device tree, a boot parameter blob) is
 * the port's business.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <span>
#include <string_view>

#include "kernel/arch/x86_64/firmware_config.hpp"

namespace cinux::arch::firmware {

/**
 * @brief         Reads a firmware string by name.
 * @param[in]     name   The key the firmware stores it under.
 * @param[out]    out    Buffer the bytes land in, NUL-terminated when it
 *                       fits.
 * @return        true when the key existed and the read happened.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline bool ReadString(std::string_view name, std::span<char> out) {
    return cinux::arch::ReadFirmwareString(name, out);
}

}  // namespace cinux::arch::firmware
