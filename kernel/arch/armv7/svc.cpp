/**
 * @file    svc.cpp
 * @brief   The armv7 supervisor-call dispatch: number in r7, args in r0-r3.
 *
 * The vector stub hands over the number and a pointer to the saved
 * argument quad. The handler writes its answer back through r0; the stub
 * stores that into the saved slot, so the user side sees it as the call's
 * result. exit never returns: it swaps the kernel context back in.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.2
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/context.hpp"
#include "kernel/arch/armv7/syscall_numbers.hpp"
#include "kernel/arch/armv7/timer.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/arch/armv7/user_access.hpp"
#include "kernel/boot/print.hpp"

namespace {

constexpr unsigned long kChunkBytes = 64;

/// Copies a user buffer in chunks and writes it; refuses bad pointers.
long do_write(unsigned long fd, unsigned long buffer, unsigned long count) {
    if (fd != 1 && fd != 2) {
        return -1;
    }
    const cinux::arch::IrqGuard kGuard;
    char chunk[kChunkBytes];
    unsigned long offset = 0;
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

/// Reads one line from the console into a user buffer, or 0 on timeout.
long do_read(unsigned long fd, unsigned long buffer, unsigned long count) {
    if (fd != 0 || count == 0) {
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

}  // namespace

extern "C" bool TaskIsDone(unsigned long index);
extern "C" unsigned long TaskExitStatus(unsigned long index);

extern "C" unsigned long c_svc_handler(unsigned long number, unsigned long* frame) {
    switch (number) {
    case cinux::armv7::syscall_nr::kRead: {
        const long result = do_read(frame[0], frame[1], frame[2]);
        if (result < 0) {
            cinux::print::Println("[armv7] svc read refused %X + %u", frame[1], frame[2]);
            return ~0UL;
        }
        cinux::print::Println("[armv7] svc read %u bytes", static_cast<unsigned long>(result));
        return static_cast<unsigned long>(result);
    }
    case cinux::armv7::syscall_nr::kWrite: {
        const long result = do_write(frame[0], frame[1], frame[2]);
        if (result < 0) {
            cinux::print::Println("[armv7] svc write refused %X + %u", frame[1], frame[2]);
            return ~0UL;
        }
        cinux::print::Println("[armv7] svc write %u bytes", static_cast<unsigned long>(result));
        return static_cast<unsigned long>(result);
    }
    case cinux::armv7::syscall_nr::kWait: {
        const unsigned long kIndex = frame[0];
        if (!TaskIsDone(kIndex)) {
            return ~0UL;
        }
        const unsigned long kStatus = TaskExitStatus(kIndex);
        const unsigned long kWord   = kStatus;
        if (!cinux::arch::CopyToUser(frame[1], &kWord, sizeof(kWord))) {
            return ~0UL;
        }
        cinux::print::Println("[armv7] wait %u -> %X", kIndex, kStatus);
        return 0;
    }
    case cinux::armv7::syscall_nr::kSchedYield:
        return 0;
    default:
        cinux::print::Println("[armv7] svc %X unknown", number);
        return ~0UL;
    }
}
