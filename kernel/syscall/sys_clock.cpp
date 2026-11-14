#include "kernel/arch/abi.hpp"
#include "kernel/syscall/sys_clock.hpp"

#include "kernel/platform/clock.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/uaccess.hpp"
#include "kernel/time/tick.hpp"

namespace cinux::syscall {

namespace {

/// Wall seconds at boot, asked of the port's clock once and never
/// again: the hardware read is slow and the wall only moves forward
/// from here. Ports without a battery-backed clock answer zero, so
/// time starts at the epoch.
unsigned long long boot_wall() {
    return platform::BootWallSeconds();
}

/// Splits monotonic nanoseconds into whole seconds and the tail.
struct ClockSplit {
    unsigned long long seconds;
    unsigned long long nanoseconds_past_second;
};

ClockSplit split_ns(unsigned long long total_ns) {
    return ClockSplit{
        .seconds                 = total_ns / 1'000'000'000ULL,
        .nanoseconds_past_second = total_ns % 1'000'000'000ULL,
    };
}

}  // namespace

unsigned long long MonotonicNanoseconds() {
    if (const unsigned long long kFine = platform::FineClockNanoseconds(); kFine != 0ULL) {
        return kFine;
    }
    const unsigned long long kTicks = time::Tick::self().since_boot();
    return kTicks * 10'000'000ULL;
}

unsigned long long RealtimeNanoseconds() {
    return (boot_wall() * 1'000'000'000ULL) + MonotonicNanoseconds();
}

long long HandleTime(const SyscallFrame& frame) {
    const unsigned long long kNow = boot_wall() + split_ns(MonotonicNanoseconds()).seconds;
    if (frame.rdi != 0 && !uaccess::CopyToUser(frame.rdi, &kNow, sizeof(kNow))) {
        return -kEfault;
    }
    return static_cast<long long>(kNow);
}

namespace {

/// Lays a two-word time record down in the caller's own shape.
long long write_time_pair(Word address, unsigned long long seconds, unsigned long long second_word,
                          cinux::arch::abi::TimePairLayout layout) {
    unsigned char raw[16] = {};
    const auto put = [&raw](unsigned long offset, unsigned long width, unsigned long long value) {
        for (unsigned long index = 0; index < width; ++index) {
            raw[offset + index] = static_cast<unsigned char>((value >> (index * 8U)) & 0xFFU);
        }
    };
    put(layout.seconds_off, layout.seconds_size, seconds);
    put(layout.nanoseconds_off, layout.nanoseconds_size, second_word);
    return uaccess::CopyToUser(address, raw, layout.bytes) ? 0 : -kEfault;
}

}  // namespace

long long HandleGettimeofday(const SyscallFrame& frame) {
    if (frame.rdi == 0 || frame.rdi >= kUserAddressLimit) {
        return -kEfault;
    }
    const ClockSplit kSince = split_ns(MonotonicNanoseconds());
    return write_time_pair(frame.rdi, boot_wall() + kSince.seconds,
                           kSince.nanoseconds_past_second / 1'000ULL,
                           arch::abi::kTimevalLayout);
}

long long HandleClockGettime(const SyscallFrame& frame) {
    if (frame.rsi == 0) {
        return -kEfault;
    }
    constexpr unsigned long long kClockRealtime  = 0;
    constexpr unsigned long long kClockMonotonic = 1;
    const ClockSplit             kSince          = split_ns(MonotonicNanoseconds());
    unsigned long long           seconds         = kSince.seconds;
    if (frame.rdi == kClockRealtime) {
        seconds += boot_wall();
    } else if (frame.rdi != kClockMonotonic) {
        return -kEinval;
    }
    return write_time_pair(frame.rsi, seconds, kSince.nanoseconds_past_second,
                           arch::abi::kTimespecLayout);
}

long long HandleClockGettime64(const SyscallFrame& frame) {
    if (frame.rsi == 0) {
        return -kEfault;
    }
    constexpr unsigned long long kClockRealtime  = 0;
    constexpr unsigned long long kClockMonotonic = 1;
    const ClockSplit             kSince          = split_ns(MonotonicNanoseconds());
    unsigned long long           seconds         = kSince.seconds;
    if (frame.rdi == kClockRealtime) {
        seconds += boot_wall();
    } else if (frame.rdi != kClockMonotonic) {
        return -kEinval;
    }
    // The time64 spelling is the 64-bit pair on every port.
    constexpr arch::abi::TimePairLayout kWide = {
        .bytes            = 16,
        .seconds_off      = 0,
        .seconds_size     = 8,
        .nanoseconds_off  = 8,
        .nanoseconds_size = 8,
    };
    return write_time_pair(frame.rsi, seconds, kSince.nanoseconds_past_second, kWide);
}

}  // namespace cinux::syscall
