/**
 * @file    sys_prlimit.hpp
 * @brief   Declarations of the resource-and-process knob syscall handlers.
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
 * @brief         prlimit64(pid, res, new, old): resource ceilings.
 *
 * @return        Zero when the row is served, or a negative errno.
 * @note          Only the calling task's own row is reachable; a
 *                stranger pid answers -EPERM. The copy-in copy-out
 *                record is one limits row of the task itself, so the
 *                width cannot drift from the table it lands in.
 * @since         0.3.0
 * @ingroup       kernel_syscall
 */
long long HandlePrlimit64(const SyscallFrame& frame);

/**
 * @brief         prctl(op, ...): process knobs, the name op served.
 *
 * @return        Zero for a served op, or a negative errno.
 * @note          PR_SET_NAME copies at most fifteen name bytes into
 *                the task's comm, PR_GET_NAME reads the whole record
 *                back; any other op answers -EINVAL.
 * @since         0.3.0
 * @ingroup       kernel_syscall
 */
long long HandlePrctl(const SyscallFrame& frame);

/// The pre-time64 limit shape: two words whose width the ABI decides.
/// A 32-bit EABI writes two four-byte words; the 64-bit spellings write
/// eight. The reader below is the only writer of this record.
struct RlimitLayout {
    unsigned long long field_bytes;  ///< 4 or 8.
};

/// The 64-bit shape, for the prlimit64 spelling.
inline constexpr RlimitLayout kRlimit64Layout{8};

/**
 * @brief         getrlimit(res, old): one row of the caller's ceilings.
 *
 * @param[in]     frame    The service frame; rdi is the resource and
 *                         rsi the destination record.
 * @param[in]     layout   The ABI's record width.
 * @return        Zero, or a negative errno.
 * @note          The old 32-bit ABI keeps this call beside prlimit64,
 *                and a libc's startup asks it directly; without the
 *                answer the caller reads whatever was on its stack.
 * @since         0.4.0
 * @ingroup       kernel_syscall
 */
long long HandleGetrlimit(const SyscallFrame& frame, const RlimitLayout& layout);

}  // namespace cinux::syscall
