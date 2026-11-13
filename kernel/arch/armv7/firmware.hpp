/**
 * @file    firmware.hpp
 * @brief   The armv7 firmware strings, read from the boot device tree.
 *
 * The x86 lane reads its strings out of the fw_cfg directory; this
 * port's firmware is the device tree QEMU hands over, so the same
 * "opt/cinux/init-env" name becomes the /opt/cinux node's init-env
 * property. One neutral name, two transports.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <span>
#include <string_view>

namespace cinux::armv7::firmware {

/// Seats the blob the boot loader placed; called once from kmain
/// before the first ReadString.
void SetDtb(unsigned long address);

/// Copies one firmware string into out, NUL-terminated. False leaves
/// out[0] = '\\0'.
[[nodiscard]] bool ReadString(std::string_view name, std::span<char> out);

}  // namespace cinux::armv7::firmware
