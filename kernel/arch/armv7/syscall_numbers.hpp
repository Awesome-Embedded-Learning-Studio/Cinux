/**
 * @file    syscall_numbers.hpp
 * @brief   The armv7 syscall numbers this lane serves, spelled as Linux does.
 *
 * Keeping the numbers identical to the arm EABI means a program compiled
 * for Linux calls them the same way; what differs is how much of each
 * call is implemented.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::armv7::syscall_nr {

inline constexpr unsigned long kExit        = 1;
inline constexpr unsigned long kOpen        = 5;
inline constexpr unsigned long kFork        = 2;
inline constexpr unsigned long kExecve      = 11;
inline constexpr unsigned long kExitGroup   = 248;
inline constexpr unsigned long kFstat64     = 197;
inline constexpr unsigned long kRead        = 3;
inline constexpr unsigned long kWrite       = 4;
inline constexpr unsigned long kSchedYield  = 158;
inline constexpr unsigned long kWait        = 114;
inline constexpr unsigned long kSleep       = 162;
inline constexpr unsigned long kLseek       = 19;
inline constexpr unsigned long kGetPid      = 20;
inline constexpr unsigned long kBrk         = 45;
inline constexpr unsigned long kGetPpid     = 64;
inline constexpr unsigned long kClose       = 6;
inline constexpr unsigned long kSignal      = 67;
inline constexpr unsigned long kGetTicks    = 78;
inline constexpr unsigned long kMmap        = 90;
inline constexpr unsigned long kMmap2       = 192;
/// The ARM-private service numbers live above the generic table: this
/// one asks the kernel to write the user TLS register, which EL0 may
/// read but never write.
inline constexpr unsigned long kSetTls      = 0x0F0005UL;

// The thread bookkeeping trio every libc startup pays: the address
// whose word the kernel clears at death, the futex robust list, and
// the restartable sequence slot. The neutral faces already exist.
inline constexpr unsigned long kWritev        = 146;
inline constexpr unsigned long kSetTidAddress = 256;
inline constexpr unsigned long kSetRobustList = 338;
inline constexpr unsigned long kRseq          = 398;
inline constexpr unsigned long kMunmap      = 91;
inline constexpr unsigned long kIoctl       = 54;
inline constexpr unsigned long kFstat       = 108;
inline constexpr unsigned long kFcntl       = 55;
inline constexpr unsigned long kGetdents    = 217;

}  // namespace cinux::armv7::syscall_nr
