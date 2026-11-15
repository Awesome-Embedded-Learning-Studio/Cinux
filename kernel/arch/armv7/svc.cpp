/**
 * @file    svc.cpp
 * @brief   The armv7 syscall table: one row per number, one entry point.
 *
 * Every supervisor call arrives here through the trap path, with the
 * user's registers already saved. The table says what handles the
 * number: exit, wait and sched_yield hand the trap to the scheduler,
 * read and write answer in place and resume the same trap. An unknown
 * number is reported and answers -1.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.4
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/syscall_numbers.hpp"
#include "kernel/arch/armv7/uart_input.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/arch/armv7/frame_bridge.hpp"
#include "kernel/syscall/sys_clock.hpp"
#include "kernel/syscall/sys_nanosleep.hpp"
#include "kernel/syscall/sys_getcwd.hpp"
#include "kernel/syscall/sys_id.hpp"
#include "kernel/syscall/sys_kill.hpp"
#include "kernel/syscall/sys_uname.hpp"
#include "kernel/syscall/sys_signal.hpp"
#include "kernel/syscall/sys_pgrp.hpp"
#include "kernel/syscall/sys_pipe.hpp"
#include "kernel/syscall/sys_fork.hpp"
#include "kernel/syscall/sys_ppoll.hpp"
#include "kernel/syscall/uaccess.hpp"
#include "kernel/syscall/sys_wait.hpp"
#include "kernel/syscall/sys_getrandom.hpp"
#include "kernel/syscall/sys_prlimit.hpp"
#include "kernel/syscall/sys_readlink.hpp"

#include "kernel/syscall/sys_brk.hpp"
#include "kernel/syscall/sys_close.hpp"
#include "kernel/syscall/sys_getpid.hpp"
#include "kernel/syscall/sys_getppid.hpp"
#include "kernel/syscall/sys_lseek.hpp"
#include "kernel/syscall/sys_mmap.hpp"
#include "kernel/syscall/sys_mprotect.hpp"
#include "kernel/syscall/sys_yield.hpp"
#include "kernel/syscall/sys_fcntl.hpp"
#include "kernel/syscall/sys_fstat.hpp"
#include "kernel/syscall/sys_statx.hpp"
#include "kernel/syscall/sys_getdents.hpp"
#include "kernel/syscall/sys_ioctl.hpp"
#include "kernel/syscall/sys_munmap.hpp"
#include "kernel/syscall/sys_open.hpp"
#include "kernel/syscall/sys_openat.hpp"
#include "kernel/syscall/sys_read.hpp"
#include "kernel/syscall/sys_exec.hpp"
#include "kernel/syscall/sys_probe.hpp"
#include "kernel/syscall/sys_writev.hpp"
#include "kernel/syscall/sys_set_tid_address.hpp"
#include "kernel/syscall/sys_write.hpp"

namespace cinux::armv7 {
void SeatReturnToKernel() __attribute__((noreturn));
void SeatExit(unsigned long status) __attribute__((noreturn));
bool SeatWorldActive();
}  // namespace cinux::armv7

namespace cinux::armv7 {

/// The EABI clone door: a libc's fork() asks for a child sharing
/// nothing but a SIGCHLD notice, with no stack and no tid words. That
/// shape is the plain fork the seat already serves; a clone that wants
/// a thread (shared vm/files/sighand, a stack, a tid word) is refused
/// as unimplemented rather than half-served.
long long HandleClone(const cinux::syscall::SyscallFrame& frame) {
    // SIGCHLD in the low byte is the "my child signals me" half; the
    // tid-word flags ride along without a stack, and the TLS word only
    // matters with CLONE_SETTLS, which a fork never sets. A clone that
    // shares the address space is a thread, and threads have no seat on
    // this port yet: that shape is refused, not half-served.
    constexpr unsigned long long kCloneVm    = 0x100;
    constexpr unsigned long long kSignalMask = 0xFF;
    if ((frame.rdi & kCloneVm) != 0 || frame.rsi != 0 ||
        (frame.rdi & kSignalMask) == 0) {
        return -38;  // ENOSYS
    }
    return cinux::syscall::HandleFork(frame);
}

/// The EABI openat door: the caller names a directory seat first and
/// only then the path. The bring-up root is the one directory that
/// exists, and AT_FDCWD names it, so the door folds to the plain open
/// face whenever the seat is that sentinel; any other seat is a
/// relative lookup the root does not have yet and is refused as such.
long long HandleOpenAt(const cinux::syscall::SyscallFrame& frame) {
    // The sentinel arrives as a 32-bit word: whichever way the caller
    // sign-extended it, the low word is the one that names AT_FDCWD.
    constexpr unsigned long kAtFdCwd = 0xFFFFFF9CUL;  // -100 in a word
    if ((frame.rdi & 0xFFFFFFFFULL) != kAtFdCwd) {
        return -9;  // EBADF: no directory seat but the current one
    }
    cinux::syscall::SyscallFrame flat = frame;
    flat.rdi                          = frame.rsi;  // path
    flat.rsi                          = frame.rdx;  // flags
    flat.rdx                          = frame.r10;  // mode
    return cinux::syscall::HandleOpen(flat);
}

/// The EABI mmap2 door: the sixth argument counts 4 KiB pages, while
/// the neutral face takes bytes. One shift bridges the two spellings.
long long HandleMmap2(const cinux::syscall::SyscallFrame& frame) {
    cinux::syscall::SyscallFrame scaled = frame;
    scaled.r9 <<= 12U;
    return cinux::syscall::HandleMmap(scaled);
}

}  // namespace cinux::armv7

namespace {

/// One neutral handler per number: the seated world's services run on
/// the same bodies the x86 lane built.
/// This ABI's old limit record: two four-byte words.
constexpr cinux::syscall::RlimitLayout kArmRlimitLayout{4};

struct NeutralService {
    unsigned long number;
    long long (*handler)(const cinux::syscall::SyscallFrame&);
};

long long HandleArmGetrlimit(const cinux::syscall::SyscallFrame& frame) {
    return cinux::syscall::HandleGetrlimit(frame, kArmRlimitLayout);
}

/// _llseek(fd, high, low, result, whence): this ABI's pre-time64 shape
/// splits the offset across two arguments and hands the new offset back
/// through a pointer. The seek itself is the neutral one; only the
/// spelling is this port's.
long long HandleArmLlseek(const cinux::syscall::SyscallFrame& frame) {
    cinux::syscall::SyscallFrame sought{};
    sought.rdi = frame.rdi;
    sought.rsi = ((frame.rsi & 0xFFFFFFFFULL) << 32U) | (frame.rdx & 0xFFFFFFFFULL);
    sought.rdx = frame.r8;  // the fifth argument slot holds trap r4, the whence
    const long long kMoved = cinux::syscall::HandleLseek(sought);
    if (kMoved < 0) {
        return kMoved;
    }
    if (frame.r10 == 0) {  // the result pointer rides in trap r3
        return -cinux::syscall::kEfault;
    }
    unsigned char record[8] = {};
    for (unsigned byte = 0; byte < 8; ++byte) {
        record[byte] = static_cast<unsigned char>((static_cast<unsigned long long>(kMoved) >>
                                                  (byte * 8U)) & 0xFFU);
    }
    if (!cinux::syscall::uaccess::CopyToUser(frame.r10, record, 8)) {
        return -cinux::syscall::kEfault;
    }
    return 0;
}

const NeutralService kNeutralServices[] = {
    {cinux::armv7::syscall_nr::kRead,   &cinux::syscall::HandleRead},
    {cinux::armv7::syscall_nr::kWrite,  &cinux::syscall::HandleWrite},
    {cinux::armv7::syscall_nr::kOpen,   &cinux::syscall::HandleOpen},
    {cinux::armv7::syscall_nr::kOpenAt, &cinux::armv7::HandleOpenAt},
    {cinux::armv7::syscall_nr::kStatx,  &cinux::syscall::HandleStatx},
    {cinux::armv7::syscall_nr::kFstatat64, &cinux::syscall::HandleNewfstatat},
    {cinux::armv7::syscall_nr::kClose,  &cinux::syscall::HandleClose},
    {cinux::armv7::syscall_nr::kLseek,  &cinux::syscall::HandleLseek},
    {cinux::armv7::syscall_nr::kGetPid, &cinux::syscall::HandleGetpid},
    // Parentage is the scheduler's bookkeeping, so the neutral face
    // answers it; the bring-up world that once owned a private parent
    // table is gone.
    {cinux::armv7::syscall_nr::kGetPpid, &cinux::syscall::HandleGetppid},
    {cinux::armv7::syscall_nr::kBrk,    &cinux::syscall::HandleBrk},
    {cinux::armv7::syscall_nr::kMmap,   &cinux::syscall::HandleMmap},
    // EABI spells mmap with a page-granular offset under its own number;
    // the adapter scales it once and the neutral face sees bytes, as the
    // first port describes them.
    {cinux::armv7::syscall_nr::kMmap2, &cinux::armv7::HandleMmap2},
    {cinux::armv7::syscall_nr::kMprotect,      &cinux::syscall::HandleMprotect},
    {cinux::armv7::syscall_nr::kWritev,        &cinux::syscall::HandleWritev},
    {cinux::armv7::syscall_nr::kSetTidAddress, &cinux::syscall::HandleSetTidAddress},
    {cinux::armv7::syscall_nr::kSetRobustList, &cinux::syscall::HandleUnsupportedProbe},
    {cinux::armv7::syscall_nr::kRseq,          &cinux::syscall::HandleUnsupportedProbe},
    {cinux::armv7::syscall_nr::kMunmap, &cinux::syscall::HandleMunmap},
    {cinux::armv7::syscall_nr::kIoctl, &cinux::syscall::HandleIoctl},
    {cinux::armv7::syscall_nr::kFcntl, &cinux::syscall::HandleFcntl},
    // EABI spells fcntl with a 64-bit-name sibling under its own number;
    // the commands are the same words, so the neutral face serves both.
    {cinux::armv7::syscall_nr::kFcntl64, &cinux::syscall::HandleFcntl},
    {cinux::armv7::syscall_nr::kFstat, &cinux::syscall::HandleFstat},
    // EABI spells the 64-bit stat pair with their own numbers; the
    // neutral face serves both spellings.
    {cinux::armv7::syscall_nr::kFstat64, &cinux::syscall::HandleFstat64},
    {cinux::armv7::syscall_nr::kGetdents, &cinux::syscall::HandleGetdents},
    // The fork face: the neutral builder copies the parent's episode
    // frame into the child before this service returns, so the port
    // entry reads the real frame, not the neutral picture.
    {cinux::armv7::syscall_nr::kFork, &cinux::syscall::HandleFork},
    {cinux::armv7::syscall_nr::kClone, &cinux::armv7::HandleClone},
    {cinux::armv7::syscall_nr::kVfork, &cinux::syscall::HandleFork},
    {cinux::armv7::syscall_nr::kPoll,           &cinux::syscall::HandlePoll},
    {cinux::armv7::syscall_nr::kPpoll,          &cinux::syscall::HandlePpoll},
    // 263 asks in the native shape the neutral writer now honours by
    // itself; 403 is the time64 spelling, two eight-byte words.
    // The sleep pair: 162 is the native spelling, 407 the EABI one that
    // carries the clock id and flags first. A shell's sleep, a paced loop
    // and any backoff timer come through one of these.
    {cinux::armv7::syscall_nr::kSleep,          &cinux::syscall::HandleNanosleep},
    {cinux::armv7::syscall_nr::kClockNanosleep, &cinux::syscall::HandleClockNanosleep},
    {cinux::armv7::syscall_nr::kClockGettime,   &cinux::syscall::HandleClockGettime},
    {cinux::armv7::syscall_nr::kClockGettime64, &cinux::syscall::HandleClockGettime64},
    {cinux::armv7::syscall_nr::kSetsid,         &cinux::syscall::HandleSetsid},
    {cinux::armv7::syscall_nr::kSetpgid,        &cinux::syscall::HandleSetpgid},
    {cinux::armv7::syscall_nr::kGetpgid,        &cinux::syscall::HandleGetpgid},
    {cinux::armv7::syscall_nr::kGetsid,         &cinux::syscall::HandleGetsid},
    {cinux::armv7::syscall_nr::kGetpgrp,        &cinux::syscall::HandleGetpgrp},
    {cinux::armv7::syscall_nr::kDup,            &cinux::syscall::HandleDup},
    {cinux::armv7::syscall_nr::kDup2,           &cinux::syscall::HandleDup2},
    {cinux::armv7::syscall_nr::kPipe,           &cinux::syscall::HandlePipe},
    {cinux::armv7::syscall_nr::kPipe2,          &cinux::syscall::HandlePipe2},
    {cinux::armv7::syscall_nr::kKill,           &cinux::syscall::HandleKill},
    {cinux::armv7::syscall_nr::kRtSigaction,    &cinux::syscall::HandleRtSigaction},
    {cinux::armv7::syscall_nr::kRtSigprocmask,  &cinux::syscall::HandleRtSigprocmask},
    {cinux::armv7::syscall_nr::kRtSigreturn,    &cinux::syscall::HandleRtSigreturn},
    {cinux::armv7::syscall_nr::kUname,          &cinux::syscall::HandleUname},
    {cinux::armv7::syscall_nr::kGetcwd,         &cinux::syscall::HandleGetcwd},
    {cinux::armv7::syscall_nr::kGetuid32,       &cinux::syscall::HandleGetuid},
    {cinux::armv7::syscall_nr::kGetgid32,       &cinux::syscall::HandleGetgid},
    {cinux::armv7::syscall_nr::kGeteuid32,      &cinux::syscall::HandleGeteuid},
    {cinux::armv7::syscall_nr::kGetegid32,      &cinux::syscall::HandleGetegid},
    // The exec face: the neutral builder stages the disk's program,
    // swaps the address space, and rewrites the frame; the bridge
    // below carries the new entry into the trap.
    {cinux::armv7::syscall_nr::kExecve, &cinux::syscall::HandleExecve},
    // Linux spells arm's wait4 as 114: the neutral wait face serves it.
    {cinux::armv7::syscall_nr::kWait, &cinux::syscall::HandleWait4},
    // A seated yield must reach the real scheduler: the tick-driven
    // preemption and the voluntary rotation share one table.
    {cinux::armv7::syscall_nr::kSchedYield, &cinux::syscall::HandleYield},
    // The startup and path trio: glibc seeds its canaries from entropy,
    // names its threads through prctl, and follows a link through
    // readlinkat. Refusing any of them makes a real program either give
    // up on a feature or fall back to a probe this port never needed.
    {cinux::armv7::syscall_nr::kGetrandom,  &cinux::syscall::HandleGetrandom},
    {cinux::armv7::syscall_nr::kPrctl,      &cinux::syscall::HandlePrctl},
    {cinux::armv7::syscall_nr::kReadlinkat, &cinux::syscall::HandleReadlinkat},
    // The pre-time64 ceilings call, in this ABI's two-word record shape.
    {cinux::armv7::syscall_nr::kGetrlimit, &HandleArmGetrlimit},
    {cinux::armv7::syscall_nr::kLlseek,     &HandleArmLlseek},
};

const NeutralService* find_neutral_service(unsigned long number) {
    for (const auto& service : kNeutralServices) {
        if (service.number == number) {
            return &service;
        }
    }
    return nullptr;
}

/// Serves one seated service call through the neutral frame.
cinux::arch::UserTrap* serve_seated_neutral(cinux::arch::UserTrap* trap, unsigned long number) {
    const NeutralService* const kService = find_neutral_service(number);
    if (kService == nullptr) {
        return nullptr;  // not ours; the caller refuses below
    }
    cinux::syscall::SyscallFrame frame{};
    cinux::arch::frame::Load(*trap, frame);
    frame.rax = static_cast<unsigned long long>(kService->handler(frame));
    cinux::arch::frame::Store(frame, *trap);
    return trap;
}

}  // namespace

extern "C" cinux::arch::UserTrap* c_svc_dispatch(cinux::arch::UserTrap* trap) {
    // The line is drained here as well as on the tick: a pasted command
    // longer than the receive FIFO would otherwise lose its tail before
    // the next tick came round.
    cinux::armv7::PollSerialKeyboard();
    const unsigned long kNumber = trap->r7;
    if (cinux::armv7::SeatWorldActive()) {
        if (kNumber == cinux::armv7::syscall_nr::kExit) {
            cinux::armv7::SeatExit(trap->r0);
        }
        // The glibc station leaves through exit_group; the bring-up
        // world is one task, so both doors lead to the same seat.
        if (kNumber == cinux::armv7::syscall_nr::kExitGroup) {
            cinux::armv7::SeatExit(trap->r0);
        }
        if (cinux::arch::UserTrap* const kServed = serve_seated_neutral(trap, kNumber)) {
            return kServed;
        }
    }
    if (kNumber == cinux::armv7::syscall_nr::kSetTls) {
        // ARM's own door behind the user TLS register: TPIDRURO is
        // read-only at EL0, so a libc installs its thread pointer by
        // asking the kernel to write the register on its behalf. The
        // value rides in r0 and the door answers zero.
        asm volatile("mcr p15, 0, %0, c13, c0, 3" : : "r"(trap->r0) : "memory");
        trap->r0 = 0;
        return trap;
    }
    if (kNumber == 0x1000UL) {
        // The seated process asks the kernel to take it back; the
        // switch into its waiting body does not come through here.
        cinux::armv7::SeatReturnToKernel();
    }
    // A number the neutral table does not own is refused, never routed
    // into a private dialect: one contract per number.
    cinux::print::Println("[armv7] svc %X (%u) refused, no neutral service", kNumber,
                          (unsigned)kNumber);
    trap->r0 = static_cast<unsigned long>(-38);  // ENOSYS
    return trap;
}
