/**
 * @file    sys_fstat.hpp
 * @brief   Declaration of the fstat(fd, out) syscall handler.
 *
 * @author  Charliechen114514
 * @date    2026-10-17
 * @version 0.3.0
 * @since   0.3.0
 * @ingroup kernel_syscall
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/syscall/syscall.hpp"

namespace cinux::syscall {

/**
 * @brief         One open's metadata, by descriptor instead of path.
 *
 * @return        Zero on success, or a negative errno.
 * @since         0.3.0
 * @ingroup       kernel_syscall
 */
long long HandleFstat(const SyscallFrame& frame);

/// The 64-bit spelling: the caller reads the wider record, so the same
/// probe is laid down in that shape.
long long HandleFstat64(const SyscallFrame& frame);

}  // namespace cinux::syscall
