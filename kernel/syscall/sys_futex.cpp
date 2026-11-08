#include "kernel/syscall/sys_futex.hpp"

#include <cinux/result.hpp>

#include "kernel/proc/futex.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/syscall_internal.hpp"

namespace cinux::syscall {

namespace {

/// futex op: park while the word holds the expected value.
inline constexpr Word kFutexWait = 0;
/// futex op: wake up to val waiters of the word.
inline constexpr Word kFutexWake = 1;
/// futex op: park while the word holds the expected value, watching a
/// subset of the word's wakeup channels.
inline constexpr Word kFutexWaitBitset = 9;
/// futex op: wake waiters whose channel mask intersects val3.
inline constexpr Word kFutexWakeBitset = 10;

}  // namespace

long long HandleFutex(const SyscallFrame& frame) {
    const Word     kUaddr   = frame.rdi;
    constexpr Word kPrivate = 128;
    const Word     kOp      = frame.rsi & ~kPrivate;
    const Word     kVal     = frame.rdx;
    // val3: the bitset for the WAIT_BITSET/WAKE_BITSET pair; unused by
    // the plain calls, which watch and offer every bit.
    const auto kBitset = static_cast<unsigned int>(frame.r9);
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
        const unsigned int kWatch = kOp == kFutexWaitBitset ? kBitset : ~0U;
        const auto         kWait = cinux::proc::futex::WaitOnUserWord(
            kUaddr, static_cast<unsigned int>(kVal), kWatch);
        if (kWait != base::KernelError::kOk) {
            return kWait == base::KernelError::kWouldBlock ? -kEagain : -kEfault;
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
