#include "kernel/proc/futex.hpp"

#include <array>
#include <utility>

#include "cinux/container/self_list.hpp"
#include "cinux/ptr.hpp"
#include "cinux/result.hpp"
#include "cinux/singleton.hpp"
#include "cinux/spin_mutex.hpp"
#include "kernel/arch/x86_64/context.hpp"
#include "kernel/arch/x86_64/irq_guard.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/sync.hpp"
#include "kernel/proc/task.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/sys_clock.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::proc::futex {

namespace {

/// Bucket count; the index is (address >> 2) scaled down to this many.
constexpr unsigned long long kBucketCount = 256;

/// The one table: a single gate over every bucket, because one core
/// never holds two buckets apart and the lockdep story stays one lock.
class FutexTable : public cinux::base::Singleton<FutexTable> {
public:
    FutexTable() = default;

    /// The gate every bucket update walks through.
    Spinlock gate;
    /// One wait list per bucket; a task parks holding its exact
    /// address so a bucket mate on a different word is never woken.
    std::array<cinux::base::container::SelfList<Task, &Task::futex_wait_link>, kBucketCount>
        buckets{};

    /// The bucket a word lands in: word granularity, spread by shift.
    [[nodiscard]] static unsigned long long bucket_for(unsigned long long uaddr) {
        return (uaddr >> 2ULL) & (kBucketCount - 1ULL);
    }
};

/// The constinit singleton cannot name its lock in a constructor, so
/// the first caller through here does it; repeating the label is
/// harmless and every later call is a plain reference fetch.
FutexTable& shared_table() {
    const arch::IrqGuard   kIrq;
    static base::SpinMutex naming;
    const auto             kGate = naming.guard();
    static bool            named = false;
    FutexTable&            table = FutexTable::self();
    if (!named) {
        table.gate.set_name("futex table");
        named = true;
    }
    return table;
}

}  // namespace

base::KernelError ParkOnWord(unsigned long long uaddr, unsigned int expected,
                             unsigned int bitset, unsigned long long deadline_ns) {
    const cinux::arch::IrqGuard kQuiet;
    Task* const                 kSelf = Scheduler::self().current();
    FutexTable&                 table = shared_table();
    // First touch outside the gate: a page that has never been made
    // present faults here, where demand::FillPage is allowed to sleep.
    unsigned int probe = 0;
    if (!cinux::syscall::uaccess::CopyFromUser(&probe, uaddr, sizeof(probe))) {
        return base::KernelError::kInvalidArgument;
    }
    {
        const SpinGuard kGuard(table.gate);
        unsigned int    present = 0;
        if (!cinux::syscall::uaccess::CopyFromUser(&present, uaddr, sizeof(present))) {
            return base::KernelError::kInvalidArgument;
        }
        if (present != expected) {
            return base::KernelError::kWouldBlock;
        }
        if (deadline_ns != ~0ULL && cinux::syscall::MonotonicNanoseconds() >= deadline_ns) {
            // The value held, but the deadline is already spent: no
            // park, no bucket seat, and the caller answers the clock.
            return base::KernelError::kTimedOut;
        }
        // About to park, so the task pointer is needed now. The read
        // that closes the lost-wakeup window runs under the gate, so it
        // must never fault: a resolver running inside the gate could
        // schedule or exit while this guard is still held. The touch
        // above made the page present; the range check keeps a
        // concurrent unmap from turning the locked read into a fault.
        // The SMAP window must never span a spin.
        if (kSelf == nullptr) {
            return base::KernelError::kInvalidArgument;
        }
        mm::VmaRegion region{};
        if (kSelf->vmas.holds() &&
            (!kSelf->vmas->lookup(uaddr, region) || !region.readable)) {
            return base::KernelError::kWouldBlock;
        }
        kSelf->futex_uaddr  = uaddr;
        kSelf->futex_bitset = bitset;
        table.buckets[FutexTable::bucket_for(uaddr)].push_back(*kSelf);
        Scheduler::self().prepare_to_wait();
        if (deadline_ns != ~0ULL) {
            // Still inside the bucket gate: the queue gate nests in
            // the documented order, and the timer can only expire the
            // task once it is on the deadline list.
            Scheduler::self().arm_timed_park(*kSelf, deadline_ns);
        }
    }
    Scheduler::self().schedule_blocked();
    if (__atomic_load_n(&kSelf->timed_park_fired, __ATOMIC_ACQUIRE)) {
        // The deadline fired the wake: the bucket seat is still held
        // unless a late waker cleared it first, so the unseat is this
        // side's own to do, and the answer is the clock's.
        __atomic_store_n(&kSelf->timed_park_fired, false, __ATOMIC_RELEASE);
        const SpinGuard kUnguard(table.gate);
        table.buckets[FutexTable::bucket_for(uaddr)].remove(*kSelf);
        return base::KernelError::kTimedOut;
    }
    Scheduler::self().cancel_timed_park(*kSelf);
    return base::KernelError::kOk;
}

base::KernelError WaitOnUserWord(unsigned long long uaddr, unsigned int expected,
                                 unsigned int bitset, unsigned long long deadline_ns) {
    return ParkOnWord(uaddr, expected, bitset, deadline_ns);
}

base::KernelError ParkKernelWord(unsigned long long uaddr, unsigned int expected,
                                 unsigned int bitset, unsigned long long deadline_ns) {
    const cinux::arch::IrqGuard kQuiet;
    Task* const                 kSelf = Scheduler::self().current();
    if ((uaddr & 3ULL) != 0ULL || kSelf == nullptr) {
        return base::KernelError::kInvalidArgument;
    }
    FutexTable& table = shared_table();
    {
        const SpinGuard kGuard(table.gate);
        // Kernel memory: this locked re-read can neither fault nor be
        // unmapped, so it takes no accessor door and consults no range
        // register — a direct load is the whole comparison.
        const unsigned int kPresent =
            *cinux::base::PtrAt<const volatile unsigned int>(uaddr);
        if (kPresent != expected) {
            return base::KernelError::kWouldBlock;
        }
        if (deadline_ns != ~0ULL && cinux::syscall::MonotonicNanoseconds() >= deadline_ns) {
            return base::KernelError::kTimedOut;
        }
        kSelf->futex_uaddr  = uaddr;
        kSelf->futex_bitset = bitset;
        table.buckets[FutexTable::bucket_for(uaddr)].push_back(*kSelf);
        Scheduler::self().prepare_to_wait();
        if (deadline_ns != ~0ULL) {
            Scheduler::self().arm_timed_park(*kSelf, deadline_ns);
        }
    }
    Scheduler::self().schedule_blocked();
    if (__atomic_load_n(&kSelf->timed_park_fired, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&kSelf->timed_park_fired, false, __ATOMIC_RELEASE);
        const SpinGuard kUnguard(table.gate);
        table.buckets[FutexTable::bucket_for(uaddr)].remove(*kSelf);
        return base::KernelError::kTimedOut;
    }
    Scheduler::self().cancel_timed_park(*kSelf);
    return base::KernelError::kOk;
}

long long WakeUserWord(unsigned long long uaddr, unsigned long long count, unsigned int bitset) {
    FutexTable&                 table = shared_table();
    const cinux::arch::IrqGuard kQuiet;
    long long                   woken = 0;
    // Detach under the gate, wake after it drops: unblock may retire a
    // group member, whose join-word clear re-enters WakeUserWord, and
    // the futex gate is not recursive.
    constexpr unsigned kWakeBatch = 16;
    Task*              batch[kWakeBatch];
    while (std::cmp_less(woken, count)) {
        unsigned taken = 0;
        {
            const SpinGuard kGuard(table.gate);
            auto&           bucket = table.buckets[FutexTable::bucket_for(uaddr)];
            Task*           node   = bucket.head();
            while (node != nullptr && taken < kWakeBatch && std::cmp_less(woken, count)) {
                Task* const kNext = bucket.after(*node);
                if (node->futex_uaddr == uaddr && (node->futex_bitset & bitset) != 0) {
                    bucket.remove(*node);
                    batch[taken++] = node;
                    ++woken;
                }
                node = kNext;
            }
        }
        for (unsigned i = 0; i < taken; ++i) {
            Scheduler::self().unblock(*batch[i]);
        }
        if (taken < kWakeBatch) {
            break;
        }
    }
    return woken;
}

void ClearTidWord(Task& dying) {
    const unsigned long long kWord          = dying.clear_child_tid;
    dying.clear_child_tid                   = 0;
    constexpr unsigned long long kWordBytes = sizeof(unsigned int);
    if (kWord < kWordBytes || kWord + kWordBytes > syscall::kUserAddressLimit ||
        !arch::IsCanonicalAddress(kWord) || !dying.vmas.holds()) {
        return;
    }
    mm::VmaRegion region{};
    const bool    kCovered = dying.vmas->lookup(static_cast<unsigned long>(kWord), region);
    const mm::VmaRegion* const kRegion = kCovered ? &region : nullptr;
    if (kRegion == nullptr || !kRegion->writable ||
        kRegion->end < static_cast<unsigned long>(kWord + kWordBytes)) {
        return;
    }
    const unsigned int kZero = 0;
    static_cast<void>(cinux::syscall::uaccess::CopyToUser(kWord, &kZero, sizeof(kZero)));
    static_cast<void>(WakeUserWord(kWord, 1));
}

}  // namespace cinux::proc::futex
