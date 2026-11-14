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
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/arch/armv7/frame_bridge.hpp"
#include "kernel/syscall/sys_fork.hpp"
#include "kernel/syscall/sys_wait.hpp"
#include "kernel/syscall/sys_brk.hpp"
#include "kernel/syscall/sys_close.hpp"
#include "kernel/syscall/sys_getpid.hpp"
#include "kernel/syscall/sys_getppid.hpp"
#include "kernel/syscall/sys_lseek.hpp"
#include "kernel/syscall/sys_mmap.hpp"
#include "kernel/syscall/sys_yield.hpp"
#include "kernel/syscall/sys_fcntl.hpp"
#include "kernel/syscall/sys_fstat.hpp"
#include "kernel/syscall/sys_getdents.hpp"
#include "kernel/syscall/sys_ioctl.hpp"
#include "kernel/syscall/sys_munmap.hpp"
#include "kernel/syscall/sys_open.hpp"
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
struct NeutralService {
    unsigned long number;
    long long (*handler)(const cinux::syscall::SyscallFrame&);
};

const NeutralService kNeutralServices[] = {
    {cinux::armv7::syscall_nr::kRead,   &cinux::syscall::HandleRead},
    {cinux::armv7::syscall_nr::kWrite,  &cinux::syscall::HandleWrite},
    {cinux::armv7::syscall_nr::kOpen,   &cinux::syscall::HandleOpen},
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
    {cinux::armv7::syscall_nr::kWritev,        &cinux::syscall::HandleWritev},
    {cinux::armv7::syscall_nr::kSetTidAddress, &cinux::syscall::HandleSetTidAddress},
    {cinux::armv7::syscall_nr::kSetRobustList, &cinux::syscall::HandleUnsupportedProbe},
    {cinux::armv7::syscall_nr::kRseq,          &cinux::syscall::HandleUnsupportedProbe},
    {cinux::armv7::syscall_nr::kMunmap, &cinux::syscall::HandleMunmap},
    {cinux::armv7::syscall_nr::kIoctl, &cinux::syscall::HandleIoctl},
    {cinux::armv7::syscall_nr::kFcntl, &cinux::syscall::HandleFcntl},
    {cinux::armv7::syscall_nr::kFstat, &cinux::syscall::HandleFstat},
    // EABI spells the 64-bit stat pair with their own numbers; the
    // neutral face serves both spellings.
    {cinux::armv7::syscall_nr::kFstat64, &cinux::syscall::HandleFstat},
    {cinux::armv7::syscall_nr::kGetdents, &cinux::syscall::HandleGetdents},
    // The fork face: the neutral builder copies the parent's episode
    // frame into the child before this service returns, so the port
    // entry reads the real frame, not the neutral picture.
    {cinux::armv7::syscall_nr::kFork, &cinux::syscall::HandleFork},
    // The exec face: the neutral builder stages the disk's program,
    // swaps the address space, and rewrites the frame; the bridge
    // below carries the new entry into the trap.
    {cinux::armv7::syscall_nr::kExecve, &cinux::syscall::HandleExecve},
    // Linux spells arm's wait4 as 114: the neutral wait face serves it.
    {cinux::armv7::syscall_nr::kWait, &cinux::syscall::HandleWait4},
    // A seated yield must reach the real scheduler: the tick-driven
    // preemption and the voluntary rotation share one table.
    {cinux::armv7::syscall_nr::kSchedYield, &cinux::syscall::HandleYield},
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
    cinux::print::Println("[armv7] svc %X refused, no neutral service", kNumber);
    trap->r0 = static_cast<unsigned long>(-38);  // ENOSYS
    return trap;
}
