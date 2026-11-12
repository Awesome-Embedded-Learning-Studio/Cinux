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
#include "kernel/arch/armv7/timer.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/user_access.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/boot/print.hpp"

namespace {

constexpr unsigned long kChunkBytes = 64;
constexpr long          kEbadf      = -9;

extern "C" unsigned long CurrentFdMask();

extern "C" cinux::arch::UserTrap* c_schedule_from_user(unsigned long number);

long do_read(unsigned long fd, unsigned long buffer, unsigned long count) {
    if ((CurrentFdMask() & (1UL << fd)) == 0 || fd != 0) {
        return kEbadf;
    }
    if (count == 0) {
        return -1;
    }
    const cinux::arch::IrqGuard kGuard;
    if (!cinux::arch::UserRangeIsMapped(buffer, count)) {
        return -1;
    }
    const auto kFrequency = cinux::armv7::TimerFrequency();
    const auto kStart     = cinux::armv7::TimerCounter();
    while (!cinux::armv7::UartReceiveReady()) {
        if ((cinux::armv7::TimerCounter() - kStart) >
            (static_cast<unsigned long long>(kFrequency) / 5U)) {
            return 0;  // 200 ms with nothing to read
        }
    }
    unsigned long received = 0;
    while (received < count && cinux::armv7::UartReceiveReady()) {
        const char          kChar = cinux::armv7::UartReadChar();
        const unsigned char kByte = static_cast<unsigned char>(kChar);
        if (!cinux::arch::CopyToUser(buffer + received, &kByte, 1)) {
            return -1;
        }
        ++received;
        if (kChar == '\n' || kChar == '\r') {
            break;
        }
    }
    return static_cast<long>(received);
}

long do_write(unsigned long fd, unsigned long buffer, unsigned long count) {
    if (fd > 2 || (CurrentFdMask() & (1UL << fd)) == 0 || (fd != 1 && fd != 2)) {
        return kEbadf;
    }
    const cinux::arch::IrqGuard kGuard;
    char                      chunk[kChunkBytes];
    unsigned long             offset = 0;
    while (offset < count) {
        const unsigned long kTake =
            (count - offset) < kChunkBytes ? (count - offset) : kChunkBytes;
        if (!cinux::arch::CopyFromUser(chunk, buffer + offset, kTake)) {
            return -1;
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

long answer_read(unsigned long* arguments) {
    return do_read(arguments[0], arguments[1], arguments[2]);
}

long answer_write(unsigned long* arguments) {
    return do_write(arguments[0], arguments[1], arguments[2]);
}

const Answer kAnswers[] = {
    {cinux::armv7::syscall_nr::kRead, "read", &answer_read},
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
           number == cinux::armv7::syscall_nr::kMmap ||
           number == cinux::armv7::syscall_nr::kWait ||
           number == cinux::armv7::syscall_nr::kSleep ||
           number == cinux::armv7::syscall_nr::kSchedYield;
}

}  // namespace

extern "C" unsigned long CurrentTaskIndex();

extern "C" cinux::arch::UserTrap* c_svc_dispatch(cinux::arch::UserTrap* trap) {
    const unsigned long kNumber = trap->r7;
    if (kNumber == cinux::armv7::syscall_nr::kGetPid) {
        trap->r0 = CurrentTaskIndex();
        cinux::print::Println("[armv7] getpid %u", trap->r0);
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
