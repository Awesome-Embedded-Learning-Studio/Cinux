/**
 * @file    abi.hpp
 * @brief   The armv7 user-ABI facts a loader or a syscall bridge must
 *          know: which ELF images may run, and how wide one ABI word is.
 *
 * These are not kernel build properties. An armv7 kernel may be compiled
 * with any word width the toolchain picks and still run only 32-bit
 * images; the word width of the initial stack, the ELF class it accepts,
 * and the machine number stamped in the header all come from the target
 * ABI, so they live with the ISA and not with the loader.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

namespace cinux::arch::abi {

/// ELF class this ABI runs: 1 is ELFCLASS32.
inline constexpr std::uint8_t kElfClass = 1;

/// ELF machine number this ABI runs: 0x28 is EM_ARM.
inline constexpr std::uint16_t kElfMachine = 0x28;

/// One word of the exec initial-stack ABI: the armv7 startup frame
/// speaks 4-byte words whatever the kernel build's own word width is.
using AbiWord = unsigned long;

/// Bytes one initial-stack word occupies on the wire.
inline constexpr unsigned long kAbiWordBytes = sizeof(AbiWord);

}  // namespace cinux::arch::abi
