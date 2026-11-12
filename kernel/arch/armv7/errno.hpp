/**
 * @file    errno.hpp
 * @brief   The negative answers an armv7 supervisor call may give.
 *
 * Values follow the arm EABI's errno so a program's expectations line up:
 * a bad pointer is EFAULT, a closed descriptor EBADF, a full window
 * ENOMEM, a malformed argument EINVAL. Wait's own "not ready" and
 * "timed out" are not errors and live with the scheduler.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::armv7::errno {

/// A pointer the kernel refused to touch.
inline constexpr long kFault = -14;  // EFAULT

/// The file has no room left.
inline constexpr long kNoSpc = -28;  // ENOSPC

/// No such file.
inline constexpr long kNoEnt = -2;  // ENOENT

/// A descriptor that is not open in this task.
inline constexpr long kBadFd = -9;  // EBADF

/// The window has no room, or a page could not be had.
inline constexpr long kNoMem = -12;  // ENOMEM

/// An argument outside the accepted shape.
inline constexpr long kInvalid = -22;  // EINVAL

}  // namespace cinux::armv7::errno
