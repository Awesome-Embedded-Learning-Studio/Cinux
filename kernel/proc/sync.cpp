/**
 * @file kernel/proc/sync.cpp
 * @brief Implementation of synchronization primitives (Spinlock, Mutex, Semaphore)
 */

#include "kernel/proc/sync.hpp"

#include <stdint.h>

#include "kernel/lib/kprintf.hpp"
#include "kernel/proc/lockdep.hpp"
#include "kernel/proc/percpu.hpp"
#include "kernel/proc/process.hpp"
#include "kernel/proc/scheduler.hpp"

namespace cinux::proc {

// ============================================================
// Spinlock implementation
// ============================================================

void Spinlock::acquire() {
    // Disable involuntary preemption BEFORE publishing lock ownership.  If a
    // timer IRQ lands after test-and-set but before the count increment, IRQ
    // exit could otherwise switch away while this CPU still owns the lock.
    preempt_disable();
    while (__atomic_test_and_set(&locked_, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
    // F4-M5 R6-Part2: record on this CPU's held stack + lock-order cycle check.
    // Compiled out (zero cost) when CINUX_LOCKDEP is off.
    lockdep_acquired(this);
}

void Spinlock::release() {
    lockdep_releasing(this);
    __atomic_clear(&locked_, __ATOMIC_RELEASE);
    // Publish the unlock before allowing an IRQ-exit preemption.
    preempt_enable();
}

Spinlock::IrqGuard::IrqGuard(Spinlock* lock) : lock_(lock) {
    __asm__ volatile("pushfq; popq %0; cli" : "=rm"(saved_flags_));
    lock_->acquire();
}

Spinlock::IrqGuard::~IrqGuard() {
    lock_->release();
    __asm__ volatile("pushq %0; popfq" : : "rm"(saved_flags_));
}

// ============================================================
// Mutex implementation
// ============================================================

void Mutex::enqueue_waiter(Task* task) {
    task->wait_next = nullptr;

    if (wait_head_ == nullptr) {
        wait_head_ = task;
        return;
    }

    Task* tail = wait_head_;
    while (tail->wait_next != nullptr) {
        tail = tail->wait_next;
    }
    tail->wait_next = task;
}

Task* Mutex::dequeue_waiter() {
    if (wait_head_ == nullptr) {
        return nullptr;
    }

    Task* task      = wait_head_;
    wait_head_      = task->wait_next;
    task->wait_next = nullptr;
    return task;
}

void Mutex::lock() {
    Task* self   = current_task();
    bool  queued = false;
    for (;;) {
        // Hold the metadata spinlock with IRQs disabled: enqueue + Blocked must
        // be atomic against unlock's ownership handoff. The local `queued`
        // survives a context switch on this task's stack and distinguishes a
        // real handoff from a recursive lock attempt.
        {
            auto g = spin_.irq_guard();
            if (owner_ == nullptr) {
                owner_ = self;
                return;
            }
            if (queued && owner_ == self) {
                return;  // unlock() dequeued us and transferred ownership
            }
            if (!queued) {
                if (self != nullptr && owner_ == self) {
                    cinux::lib::kpanic("mutex: recursive lock by tid=%lu", self->tid);
                }
                enqueue_waiter(self);
                queued = true;
            }
            Scheduler::prepare_to_wait(self);
        }

        // Scheduler::unblock() is deliberately generic: signals, timers, child
        // exit, and device queues may all call it. Treat such wakes as spurious
        // until unlock() has explicitly handed this mutex to us; never return
        // from lock() merely because Task::state became runnable.
        if (!Scheduler::schedule_blocked()) {
            return;  // single-threaded in-kernel test harness
        }
    }
}

void Mutex::unlock() {
    // Hold the spinlock only for the waiter handoff; release BEFORE unblocking
    // the new owner (never wake a task while still holding its lock).
    Task* waiter;
    {
        auto  g    = spin_.guard();
        Task* self = current_task();
        if (owner_ != self && (owner_ != nullptr || self != nullptr)) {
            cinux::lib::kpanic("mutex: unlock by non-owner");
        }
        waiter = dequeue_waiter();
        if (waiter == nullptr) {
            owner_ = nullptr;
            return;  // g releases
        }
        owner_ = waiter;
    }  // g releases (spin unlocked)

    Scheduler::unblock(waiter);
}

bool Mutex::try_lock() {
    auto g = spin_.guard();

    if (owner_ != nullptr) {
        return false;  // g releases
    }

    owner_ = current_task();
    return true;  // g releases
}

// ============================================================
// Semaphore implementation
// ============================================================

Semaphore::Semaphore(int64_t initial) : count_(initial), wait_head_(nullptr) {}

void Semaphore::enqueue_waiter(Task* task) {
    task->wait_next = nullptr;

    if (wait_head_ == nullptr) {
        wait_head_ = task;
        return;
    }

    Task* tail = wait_head_;
    while (tail->wait_next != nullptr) {
        tail = tail->wait_next;
    }
    tail->wait_next = task;
}

Task* Semaphore::dequeue_waiter() {
    if (wait_head_ == nullptr) {
        return nullptr;
    }

    Task* task      = wait_head_;
    wait_head_      = task->wait_next;
    task->wait_next = nullptr;
    return task;
}

bool Semaphore::has_waiter(Task* task) const {
    for (Task* waiter = wait_head_; waiter != nullptr; waiter = waiter->wait_next) {
        if (waiter == task) {
            return true;
        }
    }
    return false;
}

void Semaphore::post() {
    // Increment + dequeue under the lock; release BEFORE unblocking the waiter.
    Task* waiter;
    {
        auto g = spin_.guard();
        count_++;
        waiter = dequeue_waiter();
    }  // g releases

    if (waiter != nullptr) {
        Scheduler::unblock(waiter);
    }
}

void Semaphore::wait() {
    Task* self   = current_task();
    bool  queued = false;
    for (;;) {
        {
            // Decrement and enqueue exactly once. A real post grant removes us
            // from this queue under the same spinlock; unrelated scheduler wakes
            // leave us linked and therefore cannot satisfy wait().
            auto g = spin_.irq_guard();
            if (!queued) {
                count_--;
                if (count_ >= 0) {
                    return;
                }
                enqueue_waiter(self);
                queued = true;
            } else if (!has_waiter(self)) {
                return;  // post() dequeued us: the semaphore grant is ours
            }
            Scheduler::prepare_to_wait(self);
        }
        if (!Scheduler::schedule_blocked()) {
            return;  // single-threaded in-kernel test harness
        }
    }
}

bool Semaphore::try_wait() {
    auto g = spin_.guard();

    if (count_ <= 0) {
        return false;  // g releases
    }

    count_--;
    return true;  // g releases
}

int64_t Semaphore::count() const {
    return count_;
}

// ============================================================
// InterruptGuard implementation
// ============================================================

InterruptGuard::InterruptGuard() {
    __asm__ volatile("pushfq; popq %0; cli" : "=rm"(saved_flags_));
    preempt_disable();  // IRQs disabled: non-preemptable section
}

InterruptGuard::~InterruptGuard() {
    preempt_enable();
    __asm__ volatile("pushq %0; popfq" : : "rm"(saved_flags_));
}

}  // namespace cinux::proc
