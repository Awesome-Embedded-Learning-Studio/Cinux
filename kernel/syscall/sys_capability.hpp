/** @file sys_capability.hpp
 * @brief Capability ABI for the kernel's empty capability sets.
 */
#pragma once
#include "kernel/syscall/syscall.hpp"
namespace cinux::syscall {
/**
 * @brief         capget(): read a task's capability sets.
 * @param[in]     frame   The syscall frame carrying the cap_user header.
 * @return        Zero on success, or a negative errno.
 * @since         0.9.0
 * @ingroup       kernel_syscall
 */
long long HandleCapget(const SyscallFrame& frame);
/**
 * @brief         capset(): apply new capability sets to a task.
 * @param[in]     frame   The syscall frame carrying the cap_user header.
 * @return        Zero on success, or a negative errno.
 * @since         0.9.0
 * @ingroup       kernel_syscall
 */
long long HandleCapset(const SyscallFrame& frame);
}
