/**
 * @file    sys_stat.hpp
 * @brief   stat(path, out): one node's metadata for ring 3.
 *
 * @author  Charliechen114514
 * @date    2026-10-11
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_syscall
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/fs/vfs.hpp"
#include "kernel/syscall/syscall.hpp"

namespace cinux::syscall {

/**
 * @brief         stat(path, out).
 * @param[in]     frame   The trap frame; rdi carries the path address,
 *                        relative or absolute, rsi the user address
 *                        receiving a SyscallStat.
 * @return        Zero, or a negative errno.
 * @since         0.1.0
 * @ingroup       kernel_syscall
 */
long long HandleStat(const SyscallFrame& frame);

/**
 * @brief         Copy metadata for a folded kernel path to user storage.
 * @return        Zero, or a negative errno.
 * @since         0.3.0
 * @ingroup       kernel_syscall
 */
long long StatPath(const char* path, unsigned long long destination);
/**
 * @brief         Stats one relative name through a held directory's backend.
 * @param[in]     descriptor  The held directory's open descriptor.
 * @param[in]     relative    The name relative to that descriptor.
 * @param[out]    destination The user stat record.
 * @return        Zero, or a negative errno; kEopnotsupp asks the caller
 *                to fall back to string folding.
 * @since         0.9.0
 * @ingroup       kernel_syscall
 */
long long StatPathAt(unsigned long long descriptor, const char* relative,
                     unsigned long long destination);

/**
 * @brief         Fills one caller-visible stat record from an internal
 *                summary, in the layout this ABI's libc expects.
 * @param[out]    sink    Record-sized buffer.
 * @param[in]     probed  The backend's answer.
 * @note          Field offsets come from arch::abi::kStatLayout, so the
 *                neutral layer writes a 96-byte EABI record or a
 *                144-byte x86-64 one without knowing which is which.
 * @since         0.3.0
 * @ingroup       kernel_syscall
 */
void WriteStatRecord(unsigned char* sink, const fs::FsStat& probed);

/**
 * @brief         Stats the node one descriptor already holds open.
 * @param[in]     descriptor   The caller's descriptor.
 * @param[in]     destination  Where the record goes.
 * @return        0 on success, a negative errno otherwise.
 * @note          This is the AT_EMPTY_PATH case libc reaches for when
 *                it implements fstat on top of fstatat: no path is
 *                walked, the descriptor's own node answers.
 * @since         0.3.0
 * @ingroup       kernel_syscall
 */
long long StatDescriptor(unsigned long long descriptor, unsigned long long destination);

}  // namespace cinux::syscall
