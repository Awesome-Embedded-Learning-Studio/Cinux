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

/// The scatter-gather vector as this ABI lays it out: a pointer and a
/// length, eight bytes each, packed with no padding.
inline constexpr unsigned long kIovecBytes        = 16;
inline constexpr unsigned long kIovecBaseOffset   = 0;
inline constexpr unsigned long kIovecBaseBytes    = 8;
inline constexpr unsigned long kIovecLengthOffset = 8;
inline constexpr unsigned long kIovecLengthBytes  = 8;


/**
 * @brief One stat record as this ABI lays it out.
 * @note  The kernel fills a caller's record field by field at these
 *        byte offsets, so a libc built for this ABI reads its own
 *        struct stat rather than one shaped for another machine.
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

/// The x86-64 record: eight-byte fields, the shape Linux documents.
inline constexpr StatLayout kStatLayout = {
    .bytes           = 144,
    .dev_off         = 0,
    .dev_size        = 8,
    .ino_off         = 8,
    .ino_size        = 8,
    .ino_low_off     = 0,
    .ino_low_size    = 0,
    .mode_off        = 24,
    .mode_size       = 4,
    .links_off       = 16,
    .links_size      = 8,
    .uid_off         = 28,
    .uid_size        = 4,
    .gid_off         = 32,
    .gid_size        = 4,
    .rdev_off        = 40,
    .rdev_size       = 8,
    .size_off        = 48,
    .size_size       = 8,
    .blksize_off     = 56,
    .blksize_size    = 8,
    .blocks_off      = 64,
    .blocks_size     = 8,
    .atime_off       = 72,
    .atime_nsec_off  = 80,
    .mtime_off       = 88,
    .mtime_nsec_off  = 96,
    .ctime_off       = 104,
    .ctime_nsec_off  = 112,
    .time_size       = 8,
};

/**
 * @brief One fb_fix_screeninfo as this ABI lays it out.
 * @note  The record mixes 32-bit fields with `unsigned long` ones, so
 *        its width and the placement of smem_start/line_length follow
 *        the ABI rather than one machine's furniture.
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

/// x86-64: eight-byte `unsigned long`s, 80 bytes with alignment holes.
inline constexpr FbFixLayout kFbFixLayout = {
    .bytes            = 80,
    .smem_start_off   = 16,
    .smem_start_size  = 8,
    .smem_len_off     = 24,
    .line_length_off  = 48,
    .mmio_start_off   = 56,
    .mmio_start_size  = 8,
    .mmio_len_off     = 64,
    .capabilities_off = 72,
};

/**
 * @brief One struct sigaction as this ABI lays it out.
 * @note  The handler, the flags and the restorer are pointer-width, so
 *        the record's size and the mask's offset follow the ABI rather
 *        than one machine's furniture.
 */
struct SigActionLayout {
    unsigned long bytes;
    unsigned long handler_off, handler_size;
    unsigned long flags_off, flags_size;
    unsigned long restorer_off, restorer_size;
    unsigned long mask_off, mask_size;
};

/// x86-64: four eight-byte words, 32 bytes.
inline constexpr SigActionLayout kSigActionLayout = {
    .bytes        = 32,
    .handler_off  = 0,
    .handler_size = 8,
    .flags_off    = 8,
    .flags_size   = 8,
    .restorer_off = 16,
    .restorer_size = 8,
    .mask_off     = 24,
    .mask_size    = 8,
};

/**
 * @brief One struct input_event as this ABI lays it out.
 * @note  The stamp's words are `long`s, so the record is 24 bytes with
 *        an eight-byte timeval on a 64-bit port and 16 bytes with a
 *        four-byte one on EABI.
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
    .bytes        = 24,
    .seconds_off  = 0,
    .seconds_size = 8,
    .micros_off   = 8,
    .micros_size  = 8,
    .type_off     = 16,
    .type_size    = 2,
    .code_off     = 18,
    .code_size    = 2,
    .value_off    = 20,
    .value_size   = 4,
};

/**
 * @brief One struct timespec (and one timeval) as this ABI lays it out.
 * @note  The two words are `long`s, so a 64-bit port carries sixteen
 *        bytes and EABI eight; a neutral reader that assumes sixteen
 *        would take the neighbouring word as nanoseconds.
 */
struct TimePairLayout {
    unsigned long bytes;
    unsigned long seconds_off, seconds_size;
    unsigned long nanoseconds_off, nanoseconds_size;
};

inline constexpr TimePairLayout kTimespecLayout = {
    .bytes           = 16,
    .seconds_off     = 0,
    .seconds_size    = 8,
    .nanoseconds_off = 8,
    .nanoseconds_size = 8,
};

/// A timeval's second word is microseconds: same width, same offsets.
inline constexpr TimePairLayout kTimevalLayout = kTimespecLayout;

/**
 * @brief The 64-bit stat record: the shape EABI's stat64 spells.
 * @note  A 64-bit port has one stat record, so this names the same
 *        layout there. EABI's stat64 is not its stat: every field is
 *        wider, so size and ino sit four and eight bytes later.
 */
inline constexpr StatLayout kStat64Layout = kStatLayout;
}  // namespace cinux::arch::abi
