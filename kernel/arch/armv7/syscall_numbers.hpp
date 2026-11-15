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
inline constexpr unsigned long kMprotect      = 125;
inline constexpr unsigned long kOpenAt        = 322;
inline constexpr unsigned long kStatx         = 397;
inline constexpr unsigned long kFstatat64     = 327;
inline constexpr unsigned long kFcntl64       = 221;
/// EABI spells process creation with clone; a libc's fork() is
/// clone(SIGCHLD, no stack), which is the plain fork this port seats.
inline constexpr unsigned long kClone         = 120;

/// A shell's child launch. The real call shares its parent's space until
/// exec, which is an optimization this port does not need: the child
/// gets its own copy, which keeps every guarantee the caller relies on
/// (the parent's memory is not touched) and costs only a copy.
inline constexpr unsigned long kVfork         = 190;
/// Readiness and time: a desktop polls its input and asks the clock
/// every frame. EABI keeps the old syscall name for poll, and the
/// time64 spelling for the clock, beside the 32-bit one glibc still
/// probes.
inline constexpr unsigned long kPoll          = 168;
inline constexpr unsigned long kPpoll         = 336;
inline constexpr unsigned long kClockNanosleep = 407;
inline constexpr unsigned long kClockGettime   = 263;
inline constexpr unsigned long kClockGettime64 = 403;
/// The session and group words a shell's child sets up before exec.
inline constexpr unsigned long kSetpgid        = 57;
inline constexpr unsigned long kGetpgid        = 132;
inline constexpr unsigned long kGetsid         = 147;
inline constexpr unsigned long kSetsid         = 66;
inline constexpr unsigned long kGetpgrp        = 65;
/// The signal pair a shell installs before it reads: action records and
/// the mask word, both in this ABI's shape.
inline constexpr unsigned long kRtSigaction    = 174;
inline constexpr unsigned long kRtSigprocmask  = 175;
inline constexpr unsigned long kRtSigreturn    = 173;
/// The identity words and the names a shell reads before its first
/// prompt: the 32-bit id spellings EABI keeps beside uname and getcwd.
inline constexpr unsigned long kUname         = 122;
inline constexpr unsigned long kGetcwd        = 183;
inline constexpr unsigned long kGetuid32      = 199;
inline constexpr unsigned long kGetgid32      = 200;
inline constexpr unsigned long kGeteuid32     = 213;
inline constexpr unsigned long kGetegid32     = 214;
inline constexpr unsigned long kDup           = 41;
inline constexpr unsigned long kDup2          = 63;
inline constexpr unsigned long kDup3          = 358;
/// The pipe pair, the plain spelling and the flag-carrying one a libc's
/// pipe() actually asks for.
inline constexpr unsigned long kPipe          = 42;
inline constexpr unsigned long kPipe2         = 359;
inline constexpr unsigned long kKill          = 37;
inline constexpr unsigned long kSetTidAddress = 256;
inline constexpr unsigned long kSetRobustList = 338;
inline constexpr unsigned long kRseq          = 398;
inline constexpr unsigned long kMunmap      = 91;
inline constexpr unsigned long kIoctl       = 54;
inline constexpr unsigned long kFstat       = 108;
inline constexpr unsigned long kFcntl       = 55;
inline constexpr unsigned long kGetdents    = 217;
/// The three a libc's startup and path walk reach for on their own:
/// entropy for its canaries, the process word for its thread names, and
/// the symlink reader behind every "follow the link" open.
inline constexpr unsigned long kGetrandom   = 384;
inline constexpr unsigned long kPrctl       = 172;
inline constexpr unsigned long kReadlinkat  = 332;
// 239 is sendfile64, the EABI spelling; busybox's cat and cp take this
// fast path before ever staging a buffer of their own.
inline constexpr unsigned long kSendfile64 = 239;
/// The pre-time64 ceilings call glibc's startup asks for directly, and
/// the split-offset seek beside it.
inline constexpr unsigned long kGetrlimit    = 191;
inline constexpr unsigned long kLlseek       = 140;

}  // namespace cinux::armv7::syscall_nr
