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

#include "kernel/arch/armv7/errno.hpp"
#include "kernel/arch/armv7/syscall_numbers.hpp"
#include "kernel/arch/armv7/timer.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/user_access.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/arch/armv7/frame_bridge.hpp"
#include "kernel/syscall/sys_close.hpp"
#include "kernel/syscall/sys_getpid.hpp"
#include "kernel/syscall/sys_lseek.hpp"
#include "kernel/syscall/sys_open.hpp"
#include "kernel/syscall/sys_read.hpp"
#include "kernel/syscall/sys_write.hpp"

namespace {

constexpr unsigned long kChunkBytes = 64;

extern "C" bool CurrentFdReadable(unsigned long fd);
extern "C" bool CurrentFdWritable(unsigned long fd);
extern "C" bool CurrentFdIsFile(unsigned long fd);
extern "C" long FileWrite(unsigned long fd, unsigned long buffer, unsigned long count);

extern "C" cinux::arch::UserTrap* c_schedule_from_user(unsigned long number);

long do_write(unsigned long fd, unsigned long buffer, unsigned long count) {
    if (CurrentFdIsFile(fd)) {
        return FileWrite(fd, buffer, count);
    }
    if (!CurrentFdWritable(fd)) {
        return cinux::armv7::errno::kBadFd;
    }
    const cinux::arch::IrqGuard kGuard;
    char                      chunk[kChunkBytes];
    unsigned long             offset = 0;
    while (offset < count) {
        const unsigned long kTake =
            (count - offset) < kChunkBytes ? (count - offset) : kChunkBytes;
        if (!cinux::arch::CopyFromUser(chunk, buffer + offset, kTake)) {
            return cinux::armv7::errno::kFault;
        }
        for (unsigned long index = 0; index < kTake; ++index) {
            cinux::armv7::UartPutChar(chunk[index]);
        }
        offset += kTake;
    }
    return static_cast<long>(count);
}

/// A number that answers in place and lets the same trap resume.
struct Answer {
    unsigned long number;
    const char*   name;
    long (*handler)(unsigned long* arguments);
};

long answer_write(unsigned long* arguments) {
    return do_write(arguments[0], arguments[1], arguments[2]);
}

const Answer kAnswers[] = {
    {cinux::armv7::syscall_nr::kWrite, "write", &answer_write},
};

const Answer* find_answer(unsigned long number) {
    for (const auto& answer : kAnswers) {
        if (answer.number == number) {
            return &answer;
        }
    }
    return nullptr;
}

/// Numbers the scheduler owns; the table decides, not a comparison chain.
bool traps_to_scheduler(unsigned long number) {
    return number == cinux::armv7::syscall_nr::kExit ||
           number == cinux::armv7::syscall_nr::kBrk ||
           number == cinux::armv7::syscall_nr::kClose ||
           number == cinux::armv7::syscall_nr::kOpen ||
           number == cinux::armv7::syscall_nr::kLseek ||
           number == cinux::armv7::syscall_nr::kFork ||
           number == cinux::armv7::syscall_nr::kRead ||
           number == cinux::armv7::syscall_nr::kSignal ||
           number == cinux::armv7::syscall_nr::kMmap ||
           number == cinux::armv7::syscall_nr::kMunmap ||
           number == cinux::armv7::syscall_nr::kFstat ||
           number == cinux::armv7::syscall_nr::kWait ||
           number == cinux::armv7::syscall_nr::kSleep ||
           number == cinux::armv7::syscall_nr::kSchedYield;
}

}  // namespace

extern "C" unsigned long CurrentTaskIndex();
extern "C" unsigned long CurrentTaskParent();
extern "C" unsigned long CurrentTick();

namespace cinux::armv7 {
void SeatReturnToKernel() __attribute__((noreturn));
void SeatExit(unsigned long status) __attribute__((noreturn));
bool SeatWorldActive();
}

namespace {

/// One neutral handler per number: the seated world's services run on
/// the same bodies the x86 lane built.
struct NeutralService {
    unsigned long number;
    long long (*handler)(const cinux::syscall::SyscallFrame&);
};

const NeutralService kNeutralServices[] = {
    {cinux::armv7::syscall_nr::kRead,   &cinux::syscall::HandleRead},
    {cinux::armv7::syscall_nr::kOpen,   &cinux::syscall::HandleOpen},
    {cinux::armv7::syscall_nr::kClose,  &cinux::syscall::HandleClose},
    {cinux::armv7::syscall_nr::kLseek,  &cinux::syscall::HandleLseek},
    {cinux::armv7::syscall_nr::kGetPid, &cinux::syscall::HandleGetpid},
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
        return nullptr;  // not ours; the caller falls through
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
        if (cinux::arch::UserTrap* const kServed = serve_seated_neutral(trap, kNumber)) {
            return kServed;
        }
    }
    if (kNumber == 0x1000UL) {
        // The seated process asks the kernel to take it back; the
        // switch into its waiting body does not come through here.
        cinux::armv7::SeatReturnToKernel();
    }
    if (kNumber == cinux::armv7::syscall_nr::kGetPid) {
        trap->r0 = CurrentTaskIndex();
        cinux::print::Println("[armv7] getpid %u", trap->r0);
        return trap;
    }
    if (kNumber == cinux::armv7::syscall_nr::kGetTicks) {
        trap->r0 = CurrentTick();
        cinux::print::Println("[armv7] getticks %u", trap->r0);
        return trap;
    }
    if (kNumber == cinux::armv7::syscall_nr::kGetPpid) {
        trap->r0 = CurrentTaskParent();
        cinux::print::Println("[armv7] getppid %u", trap->r0);
        return trap;
    }
    if (traps_to_scheduler(kNumber)) {
        return c_schedule_from_user(kNumber);
    }
    const Answer* const kAnswer = find_answer(kNumber);
    if (kAnswer == nullptr) {
        cinux::print::Println("[armv7] svc %X unknown", kNumber);
        trap->r0 = ~0UL;
        return trap;
    }
    unsigned long arguments[3] = {trap->r0, trap->r1, trap->r2};
    const long    kResult      = kAnswer->handler(arguments);
    if (kResult < 0) {
        cinux::print::Println("[armv7] svc %s refused %d at %X + %u", kAnswer->name,
                              static_cast<int>(kResult), arguments[1], arguments[2]);
        trap->r0 = static_cast<unsigned long>(kResult);
    } else {
        cinux::print::Println("[armv7] svc %s %u bytes", kAnswer->name,
                              static_cast<unsigned long>(kResult));
        trap->r0 = static_cast<unsigned long>(kResult);
    }
    return trap;
}
