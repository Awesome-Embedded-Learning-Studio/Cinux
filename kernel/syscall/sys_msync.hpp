/**
 * @file    sys_msync.hpp
 * @brief   msync(2): the write-back promise over one mapped span.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_syscall
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/syscall/syscall.hpp"

namespace cinux::syscall {

/**
 * @brief         Answers the write-back promise for one mapped span.
 * @param[in]     frame rdi is the span's start, rsi its length, rdx
 *                the flag word (MS_SYNC, MS_ASYNC, MS_INVALIDATE).
 * @return        Zero: this kernel's mappings are coherent the moment
 *                they are written -- device frames are the device's
 *                own, private copies live in memory -- so the promise
 *                holds and no flush is owed. ENOMEM names an unmapped
 *                span, EINVAL a bad flag word.
 * @since         0.1.0
 * @ingroup       kernel_syscall
 */
long long HandleMsync(const SyscallFrame& frame);

}  // namespace cinux::syscall
