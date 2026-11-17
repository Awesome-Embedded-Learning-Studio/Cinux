#include "kernel/syscall/sys_itimer.hpp"

#include "kernel/arch/abi.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/syscall/sys_clock.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/syscall_internal.hpp"
#include "kernel/syscall/uaccess.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/vmm.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/boot/print.hpp"

namespace cinux::syscall {
namespace {
constexpr Word kNanosecondsPerSecond      = 1'000'000'000;
constexpr Word kNanosecondsPerMicrosecond = 1'000;
// The caller's itimerval is the ABI's record, not the kernel's: a timeval
// is two `long` words, so the record is sixteen bytes on EABI and
// thirty-two on the 64-bit port. The neutral Word nanoseconds stay the
// internal currency, and every conversion between the two happens here.
using cinux::arch::abi::Itimerval;
using cinux::arch::abi::Timeval;
using TimeWord = decltype(Timeval::seconds);

bool to_nanoseconds(const Timeval& value, Word& out) {
    if (value.seconds < 0 || value.microseconds < 0 || value.microseconds >= 1'000'000) {
        return false;
    }
    const auto kTail = static_cast<Word>(value.microseconds) * kNanosecondsPerMicrosecond;
    const auto kSecs = static_cast<Word>(value.seconds);
    if (kSecs > (~0ULL - kTail) / kNanosecondsPerSecond) {
        return false;
    }
    out = (kSecs * kNanosecondsPerSecond) + kTail;
    return true;
}

Timeval from_nanoseconds(Word value) {
    const Word kMicros =
        (value / kNanosecondsPerMicrosecond) + (value % kNanosecondsPerMicrosecond != 0 ? 1 : 0);
    return {
        .seconds      = static_cast<TimeWord>(kMicros / 1'000'000),
        .microseconds = static_cast<TimeWord>(kMicros % 1'000'000),
    };
}
}

long long HandleGetitimer(const SyscallFrame& frame) {
    if (frame.rdi != 0) {
        return -kEinval;
    }
    proc::RealTimerValue timer{};
    proc::Scheduler::self().real_timer(*CurrentTask(), MonotonicNanoseconds(), timer, false);
    const Itimerval kOld{
        .interval = from_nanoseconds(timer.interval_ns),
        .value    = from_nanoseconds(timer.value_ns),
    };
    return uaccess::CopyToUser(frame.rsi, &kOld, sizeof(kOld)) ? 0 : -kEfault;
}

long long HandleSetitimer(const SyscallFrame& frame) {
    if (frame.rdi != 0) {
        return -kEinval;
    }
    Itimerval wanted{};
    if (frame.rsi != 0 && !uaccess::CopyFromUser(&wanted, frame.rsi, sizeof(wanted))) {
        return -kEfault;
    }
    proc::RealTimerValue previous{};
    proc::RealTimerValue armed{};
    const Word           kNow = MonotonicNanoseconds();
    if (!to_nanoseconds(wanted.value, armed.value_ns) ||
        !to_nanoseconds(wanted.interval, armed.interval_ns)) {
        return -kEinval;
    }
    if (armed.value_ns > ~0ULL - kNow) {
        return -kEinval;
    }
    // Read the running timer before arming. real_timer writes the state it
    // replaced back through its out-parameter, but keeping the read in its
    // own call leaves "what the caller had" apart from "what we set".
    proc::Scheduler::self().real_timer(*CurrentTask(), kNow, previous, false);
    proc::Scheduler::self().real_timer(*CurrentTask(), kNow, armed, true);
    if (frame.rdx != 0) {
        const Itimerval kOld{
            .interval = from_nanoseconds(previous.interval_ns),
            .value    = from_nanoseconds(previous.value_ns),
        };
        if (!uaccess::CopyToUser(frame.rdx, &kOld, sizeof(kOld))) {
            return -kEfault;
        }
    }
    return 0;
}

long long HandleAlarm(const SyscallFrame& frame) {
    proc::RealTimerValue timer{
        .value_ns = static_cast<unsigned int>(frame.rdi) * kNanosecondsPerSecond,
    };
    proc::Scheduler::self().real_timer(*CurrentTask(), MonotonicNanoseconds(), timer, true);
    return static_cast<long long>((timer.value_ns / kNanosecondsPerSecond) +
                                  (timer.value_ns % kNanosecondsPerSecond != 0 ? 1 : 0));
}

}
