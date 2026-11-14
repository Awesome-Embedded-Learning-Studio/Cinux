/**
 * @file    sys_statx.hpp
 * @brief   Declaration of the statx() syscall handler.
 *
 * statx is the one metadata call whose record layout is architecture
 * independent: fixed-width fields at fixed offsets, identical on every
 * port. That makes it the neutral face for callers that only need a
 * node's shape, and the one place a 32-bit port does not have to
 * describe a legacy struct stat to the kernel.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.3.0
 * @ingroup kernel_syscall
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/syscall/syscall.hpp"

namespace cinux::syscall {

/**
 * @brief         statx(dirfd, path, flags, mask, buf): one node's shape
 *                in the fixed-width record every port shares.
 * @param[in]     frame   rdi dirfd, rsi path, rdx flags, r10 mask,
 *                        r8 destination record.
 * @return        0 on success, a negative errno otherwise.
 * @note          The record is 256 bytes with no padding a compiler may
 *                invent: every field is written at the offset Linux
 *                defines, so a libc built for any of our ports reads the
 *                same bytes.
 * @since         0.3.0
 * @ingroup       kernel_syscall
 */
long long HandleStatx(const SyscallFrame& frame);

/// Bytes one statx record occupies on the wire.
inline constexpr unsigned long kStatxRecordBytes = 256;

}  // namespace cinux::syscall
