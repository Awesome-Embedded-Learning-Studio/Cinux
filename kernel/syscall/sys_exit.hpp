/**
 * @file    sys_exit.hpp
 * @brief   Declaration of the exit(code) syscall handler.
 *
 * @author  Charliechen114514
 * @date    2026-10-07
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_syscall
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/syscall/syscall.hpp"

namespace cinux::syscall {

/**
 * @brief         End the calling task; never returns.
 *
 * @return        What ring 3 receives: a count, a code, or a negative
 *                errno.
 * @since         0.1.0
 * @ingroup       kernel_syscall
 */
[[noreturn]] long long HandleExit(const SyscallFrame& frame);

/**
 * @brief         End every task in the calling task's thread group.
 *
 * @param[in]     frame   The syscall frame; rdi holds the exit word.
 * @return        Never returns.
 * @note          exit() ends one thread; exit_group ends the process.
 *                The scheduler retires the members that can be taken
 *                off their queues on the spot and marks the parked
 *                ones to die at their next wake, so a group whose
 *                members are all asleep still converges on death.
 * @since         0.9.0
 * @ingroup       kernel_syscall
 */
[[noreturn]] long long HandleExitGroup(const SyscallFrame& frame);

}  // namespace cinux::syscall
