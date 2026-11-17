#include "kernel/syscall/sys_itimer.hpp"

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
// The ARM32 Linux ABI's own shapes: tv_sec and tv_usec are 32-bit longs,
// so a timeval is eight bytes and an itimerval sixteen. The neutral 64-bit
// Words are the internal currency; the conversion to and from the ABI's
// fields happens here and nowhere else.
struct Timeval {
    std::int32_t seconds      = 0;
    std::int32_t microseconds = 0;
};
struct Itimerval {
    Timeval interval;
    Timeval value;
};
static_assert(sizeof(Timeval) == 8);
static_assert(sizeof(Itimerval) == 16);

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
        .seconds      = static_cast<std::int32_t>(kMicros / 1'000'000),
        .microseconds = static_cast<std::int32_t>(kMicros % 1'000'000),
    };
}
}

long long HandleGetitimer(const SyscallFrame& frame) {
    if (frame.rdi != 0) {
        return -kEinval;
    }
    proc::RealTimerValue timer;
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
    // The previous state comes back through the out-parameter: a fresh
    // caller reads zeros, a re-arm reads the still-running timer.
    proc::Scheduler::self().real_timer(*CurrentTask(), kNow, previous, false);
    proc::Scheduler::self().real_timer(*CurrentTask(), kNow, armed, true);
    if (frame.rdx != 0) {
        const Itimerval kOld{
            .interval = from_nanoseconds(armed.interval_ns),
            .value    = from_nanoseconds(armed.value_ns),
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
