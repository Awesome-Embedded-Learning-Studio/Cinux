/**
 * @file    firmware.cpp
 * @brief   Splits a fw_cfg-style name into a dtb node path and property.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/firmware.hpp"

#include "kernel/arch/armv7/fdt.hpp"

namespace cinux::arch::firmware {

namespace {

std::uint32_t g_dtb = 0;

}  // namespace

void SetDtb(unsigned long address) {
    g_dtb = static_cast<std::uint32_t>(address);
}

bool ReadString(std::string_view name, std::span<char> out) {
    if (out.empty()) {
        return false;
    }
    out[0] = '\0';
    if (g_dtb == 0 || name.empty() || name.size() >= 56 || name.front() != 'o' ||
        name.find('/') == std::string_view::npos) {
        return false;
    }
    // "opt/cinux/init-env" -> node "/opt/cinux", property "init-env":
    // the last component is the property, the rest is the node path.
    const std::size_t kLast = name.rfind('/');
    char              node[48];
    std::uint32_t     used = 0;
    node[used++]           = '/';
    for (std::size_t index = 0; index < kLast; ++index) {
        if (used >= sizeof(node) - 1) {
            return false;
        }
        node[used++] = name[index];
    }
    if (used < 2) {
        return false;  // no node component before the property
    }
    node[used] = '\0';
    const std::string_view kProperty = name.substr(name.rfind('/') + 1);
    char property[56];
    if (kProperty.size() >= sizeof(property)) {
        return false;
    }
    for (std::size_t index = 0; index < kProperty.size(); ++index) {
        property[index] = kProperty[index];
    }
    property[kProperty.size()] = '\0';

    cinux::armv7::PropertyBytes found{};
    if (!cinux::armv7::ReadProperty(g_dtb, node, property, found) || found.len == 0 ||
        found.len >= out.size()) {
        return false;
    }
    for (std::uint32_t index = 0; index < found.len; ++index) {
        out[index] = static_cast<char>(found.bytes[index]);
    }
    out[found.len] = '\0';
    return true;
}

}  // namespace cinux::arch::firmware
