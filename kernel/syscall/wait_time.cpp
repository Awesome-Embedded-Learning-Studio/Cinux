#include "kernel/arch/abi.hpp"
#include "kernel/syscall/wait_time.hpp"

#include "cinux/time.hpp"
#include "kernel/syscall/sys_clock.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/syscall_internal.hpp"
#include "kernel/syscall/uaccess.hpp"
namespace cinux::syscall {
long long ReadTimespec(Word address, Timespec& record) {
    // The record's own width belongs to the ABI: EABI's two 32-bit
    // words are eight bytes, a 64-bit port's are sixteen. Reading the
    // 64-bit shape on EABI would take the neighbouring word as the
    // nanosecond part.
    constexpr arch::abi::TimePairLayout kLayout = arch::abi::kTimespecLayout;
    unsigned char raw[kLayout.bytes] = {};
    if (!uaccess::CopyFromUser(raw, address, sizeof(raw))) {
        return -kEfault;
    }
    const auto take = [&raw](unsigned long offset, unsigned long width) {
        unsigned long long value = 0;
        for (unsigned long index = 0; index < width; ++index) {
            value |= static_cast<unsigned long long>(raw[offset + index]) << (index * 8U);
        }
        // A four-byte word is signed: -1 is a negative time, not 4G.
        if (width == 4 && (raw[offset + 3] & 0x80U) != 0) {
            value |= 0xFFFFFFFF00000000ULL;
        }
        return value;
    };
    Timespec staged{};
    staged.seconds     = static_cast<long long>(take(kLayout.seconds_off, kLayout.seconds_size));
    staged.nanoseconds = static_cast<long long>(take(kLayout.nanoseconds_off,
                                                     kLayout.nanoseconds_size));
    if (staged.seconds < 0 || staged.nanoseconds < 0 || staged.nanoseconds >= 1'000'000'000LL) {
        return -kEinval;
    }
    record = staged;
    return 0;
}

long long ReadWaitDeadline(Word address, unsigned long long& deadline) {
    deadline = ~0ULL;
    if (address == 0) {
        return 0;
    }
    Timespec asked{};
    if (const auto kError = ReadTimespec(address, asked); kError != 0) {
        return kError;
    }
    deadline = base::time::DeadlineAfter(
                   base::time::Nanoseconds{MonotonicNanoseconds()},
                   base::time::Seconds{static_cast<unsigned long long>(asked.seconds)},
                   base::time::Nanoseconds{static_cast<unsigned long long>(asked.nanoseconds)})
                   .value;
    return 0;
}

unsigned long long MillisecondDeadline(int milliseconds) {
    if (milliseconds < 0) {
        return ~0ULL;
    }
    return base::time::DeadlineAfter(
               base::time::Nanoseconds{MonotonicNanoseconds()}, base::time::Seconds{0},
               base::time::ToNanoseconds(
                   base::time::Milliseconds{static_cast<unsigned long long>(milliseconds)}))
        .value;
}

}
