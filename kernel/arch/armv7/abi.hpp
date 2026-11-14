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

/// The scatter-gather vector as this ABI lays it out: a pointer and a
/// length, four bytes each, with no padding between them.
inline constexpr unsigned long kIovecBytes        = 8;
inline constexpr unsigned long kIovecBaseOffset   = 0;
inline constexpr unsigned long kIovecBaseBytes    = 4;
inline constexpr unsigned long kIovecLengthOffset = 4;
inline constexpr unsigned long kIovecLengthBytes  = 4;


/**
 * @brief One stat record as this ABI lays it out.
 * @note  This is the EABI struct stat64: a 64-bit device word with four
 *        pad bytes, a legacy 32-bit inode beside the real one, 32-bit
 *        permission and link words, 64-bit size and blocks, four-byte
 *        timestamps, and the 64-bit inode at the tail. A libc built for
 *        the port reads exactly these offsets.
 */
struct StatLayout {
    unsigned long bytes;
    unsigned long dev_off, dev_size;
    unsigned long ino_off, ino_size;
    unsigned long ino_low_off, ino_low_size;  ///< legacy 32-bit ino, zero when absent
    unsigned long mode_off, mode_size;
    unsigned long links_off, links_size;
    unsigned long uid_off, uid_size;
    unsigned long gid_off, gid_size;
    unsigned long rdev_off, rdev_size;
    unsigned long size_off, size_size;
    unsigned long blksize_off, blksize_size;
    unsigned long blocks_off, blocks_size;
    unsigned long atime_off, atime_nsec_off;
    unsigned long mtime_off, mtime_nsec_off;
    unsigned long ctime_off, ctime_nsec_off;
    unsigned long time_size;
};

/// The EABI record: mixed widths, 96 bytes, the shape the ABI defines.
inline constexpr StatLayout kStatLayout = {
    .bytes           = 96,
    .dev_off         = 0,
    .dev_size        = 8,
    .ino_off         = 88,
    .ino_size        = 8,
    .ino_low_off     = 12,
    .ino_low_size    = 4,
    .mode_off        = 16,
    .mode_size       = 4,
    .links_off       = 20,
    .links_size      = 4,
    .uid_off         = 24,
    .uid_size        = 4,
    .gid_off         = 28,
    .gid_size        = 4,
    .rdev_off        = 32,
    .rdev_size       = 8,
    .size_off        = 44,
    .size_size       = 8,
    .blksize_off     = 52,
    .blksize_size    = 4,
    .blocks_off      = 56,
    .blocks_size     = 8,
    .atime_off       = 64,
    .atime_nsec_off  = 68,
    .mtime_off       = 72,
    .mtime_nsec_off  = 76,
    .ctime_off       = 80,
    .ctime_nsec_off  = 84,
    .time_size       = 4,
};

/**
 * @brief One fb_fix_screeninfo as this ABI lays it out.
 * @note  EABI keeps `unsigned long` at four bytes, so the record is 64
 *        bytes and every field after smem_start sits twelve bytes
 *        earlier than the 64-bit shape.
 */
struct FbFixLayout {
    unsigned long bytes;
    unsigned long smem_start_off, smem_start_size;
    unsigned long smem_len_off;
    unsigned long line_length_off;
    unsigned long mmio_start_off, mmio_start_size;
    unsigned long mmio_len_off;
    unsigned long capabilities_off;
};

inline constexpr FbFixLayout kFbFixLayout = {
    .bytes            = 64,
    .smem_start_off   = 16,
    .smem_start_size  = 4,
    .smem_len_off     = 20,
    .line_length_off  = 44,
    .mmio_start_off   = 48,
    .mmio_start_size  = 4,
    .mmio_len_off     = 52,
    .capabilities_off = 60,
};

/**
 * @brief One struct sigaction as this ABI lays it out.
 * @note  EABI's handler, flags and restorer are four bytes each, so the
 *        record is 20 bytes and the mask sits at byte 12 — twelve bytes
 *        earlier than the 64-bit shape.
 */
struct SigActionLayout {
    unsigned long bytes;
    unsigned long handler_off, handler_size;
    unsigned long flags_off, flags_size;
    unsigned long restorer_off, restorer_size;
    unsigned long mask_off, mask_size;
};

inline constexpr SigActionLayout kSigActionLayout = {
    .bytes         = 20,
    .handler_off   = 0,
    .handler_size  = 4,
    .flags_off     = 4,
    .flags_size    = 4,
    .restorer_off  = 8,
    .restorer_size = 4,
    .mask_off      = 12,
    .mask_size     = 8,
};

/**
 * @brief One struct input_event as this ABI lays it out.
 * @note  EABI's timeval is two four-byte words, so the record is 16
 *        bytes and every field after the stamp sits eight bytes earlier
 *        than the 64-bit shape.
 */
struct InputEventLayout {
    unsigned long bytes;
    unsigned long seconds_off, seconds_size;
    unsigned long micros_off, micros_size;
    unsigned long type_off, type_size;
    unsigned long code_off, code_size;
    unsigned long value_off, value_size;
};

inline constexpr InputEventLayout kInputEventLayout = {
    .bytes        = 16,
    .seconds_off  = 0,
    .seconds_size = 4,
    .micros_off   = 4,
    .micros_size  = 4,
    .type_off     = 8,
    .type_size    = 2,
    .code_off     = 10,
    .code_size    = 2,
    .value_off    = 12,
    .value_size   = 4,
};
}  // namespace cinux::arch::abi
