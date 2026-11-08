#include "kernel/proc/futex.hpp"

#include <array>
#include <utility>

#include "cinux/container/self_list.hpp"
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

base::KernelError ParkOnWord(unsigned long long uaddr, unsigned int expected, unsigned int bitset) {
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
    }
    Scheduler::self().schedule_blocked();
    return base::KernelError::kOk;
}

base::KernelError WaitOnUserWord(unsigned long long uaddr, unsigned int expected,
                                 unsigned int bitset) {
    return ParkOnWord(uaddr, expected, bitset);
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
