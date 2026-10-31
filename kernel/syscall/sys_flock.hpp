/**
 * @file sys_flock.hpp
 * @brief flock(2): whole-file advisory locks.
 * @author Charliechen114514
 * @date 2026-10-31
 * @version 0.1
 * @since 0.1.0
 * @ingroup kernel_syscall
 * @copyright Copyright (c) 2026
 */
#pragma once

#include "kernel/syscall/syscall.hpp"

namespace cinux::syscall {
/**
 * @brief Places, upgrades, drops, or asks about an open's flock lock.
 * @param[in] frame The descriptor in rdi, the operation word in rsi.
 * @return Zero on success, or negative EBADF, EINVAL, EAGAIN or EINTR.
 * @note The registry answers what is grantable now; this handler owns
 * the waiting. LOCK_NB turns a busy lock into EAGAIN, a blocking call
 * yields until the conflict leaves or a signal arrives, and an open
 * that re-locks replaces the lock it held, so one call upgrades or
 * downgrades in place. The lock dies with the open's last descriptor
 * even without LOCK_UN.
 */
long long HandleFlock(const SyscallFrame& frame);
}
