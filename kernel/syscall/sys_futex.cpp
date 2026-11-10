#include "kernel/syscall/sys_futex.hpp"

#include <cinux/result.hpp>

#include "kernel/syscall/sys_clock.hpp"
#include "kernel/proc/futex.hpp"
#include "kernel/syscall/wait_time.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/syscall_internal.hpp"

namespace cinux::syscall {

namespace {

/// futex op: park while the word holds the expected value.
inline constexpr Word kFutexWait       = 0;
/// futex op: wake up to val waiters of the word.
inline constexpr Word kFutexWake       = 1;
/// futex op: park while the word holds the expected value, watching a
/// subset of the word's wakeup channels.
inline constexpr Word kFutexWaitBitset = 9;
/// futex op: wake waiters whose channel mask intersects val3.
inline constexpr Word kFutexWakeBitset = 10;

}  // namespace

long long HandleFutex(const SyscallFrame& frame) {
    const Word     kUaddr   = frame.rdi;
    constexpr Word kPrivate      = 128;
    constexpr Word kClockRealtime = 256;
    // Both flag bits ride the op word; the operation number itself is
    // what remains once they lift off.
    const Word     kOp           = frame.rsi & ~(kPrivate | kClockRealtime);
    const Word     kVal     = frame.rdx;
    // val3: the bitset for the WAIT_BITSET/WAKE_BITSET pair; unused by
    // the plain calls, which watch and offer every bit.
    const auto     kBitset  = static_cast<unsigned int>(frame.r9);
    if ((kUaddr & 3ULL) != 0ULL) {
        return -kEinval;
    }
    if (kOp == kFutexWait || kOp == kFutexWaitBitset) {
        if (!UserRangeReadable(kUaddr, sizeof(unsigned int))) {
            return -kEfault;
        }
        if (kOp == kFutexWaitBitset && kBitset == 0) {
            return -kEinval;
        }
        const bool kRealtime = (frame.rsi & kClockRealtime) != 0;
        if (kRealtime && kOp != kFutexWaitBitset) {
            // Linux grants the realtime selection to the bitset pair only.
            return -kEinval;
        }
        // r10 names the timeout record. Plain WAIT carries a relative
        // span; WAIT_BITSET an absolute stamp on the monotonic rail by
        // default, which the realtime selection moves by the offset
        // between the two rails right now.
        unsigned long long deadline = ~0ULL;
        if (frame.r10 != 0) {
            if (kOp == kFutexWait) {
                const long long kParsed = ReadWaitDeadline(frame.r10, deadline);
                if (kParsed != 0) {
                    return kParsed;
                }
            } else {
                Timespec stamp{};
                const long long kParsed = ReadTimespec(frame.r10, stamp);
                if (kParsed != 0) {
                    return kParsed;
                }
                constexpr unsigned long long kSecond = 1000000000ULL;
                if (stamp.seconds >= 0 &&
                    static_cast<unsigned long long>(stamp.seconds) <=
                        (~0ULL - static_cast<unsigned long long>(stamp.nanoseconds)) / kSecond) {
                    const unsigned long long kAbsolute =
                        static_cast<unsigned long long>(stamp.seconds) * kSecond +
                        static_cast<unsigned long long>(stamp.nanoseconds);
                    if (!kRealtime) {
                        deadline = kAbsolute;
                    } else {
                        const unsigned long long kNowReal = RealtimeNanoseconds();
                        const unsigned long long kNowMono = MonotonicNanoseconds();
                        const unsigned long long kOffset  = kNowReal >= kNowMono ? kNowReal - kNowMono : 0;
                        deadline = kAbsolute > kOffset ? kAbsolute - kOffset : 0;
                    }
                }
            }
        }
        const unsigned int kWatch = kOp == kFutexWaitBitset ? kBitset : ~0U;
        const auto         kWait  = cinux::proc::futex::WaitOnUserWord(
            kUaddr, static_cast<unsigned int>(kVal), kWatch, deadline);
        if (kWait != base::KernelError::kOk) {
            return kWait == base::KernelError::kWouldBlock
                       ? -kEagain
                       : (kWait == base::KernelError::kTimedOut ? -kEtimedout : -kEfault);
        }
        return 0;
    }
    if (kOp == kFutexWake || kOp == kFutexWakeBitset) {
        if (!UserRangeReadable(kUaddr, sizeof(unsigned int))) {
            return -kEfault;
        }
        if (kOp == kFutexWakeBitset && kBitset == 0) {
            return -kEinval;
        }
        const unsigned int kOffer = kOp == kFutexWakeBitset ? kBitset : ~0U;
        return cinux::proc::futex::WakeUserWord(kUaddr, kVal, kOffer);
    }
    return -kEnosys;
}

}  // namespace cinux::syscall
