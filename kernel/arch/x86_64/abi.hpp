/**
 * @file    abi.hpp
 * @brief   The x86-64 user-ABI facts a loader or a syscall bridge must
 *          know: which ELF images may run, and how wide one ABI word is.
 *
 * These are not kernel build properties. An x86-64 kernel may be compiled
 * by a 64-bit host and still run only 64-bit images; the word width of the
 * initial stack, the ELF class it accepts, and the machine number stamped
 * in the header all come from the target ABI, so they live with the ISA
 * and not with the loader.
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

namespace cinux::arch::abi {

/// ELF class this ABI runs: 2 is ELFCLASS64.
inline constexpr std::uint8_t kElfClass = 2;

/// ELF machine number this ABI runs: 0x3E is EM_X86_64.
inline constexpr std::uint16_t kElfMachine = 0x3E;

/// One word of the exec initial-stack ABI: the System V startup frame
/// speaks 8-byte words whatever the kernel build's own word width is.
using AbiWord = unsigned long long;

/// Bytes one initial-stack word occupies on the wire.
inline constexpr unsigned long kAbiWordBytes = sizeof(AbiWord);

}  // namespace cinux::arch::abi
