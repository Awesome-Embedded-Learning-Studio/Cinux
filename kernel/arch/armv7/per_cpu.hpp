/**
 * @file    per_cpu.hpp
 * @brief   The armv7 per-CPU block, reached through TPIDRPRW.
 *
 * x86 keeps this pointer in a segment base; armv7 reserves one CP15
 * register for exactly this purpose, so finding the block is one MRC and
 * never depends on a live segment. Only the fields the bring-up lane
 * uses exist here; the scheduler's counters join them when they arrive.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <atomic>

namespace cinux::proc {
struct Task;
}

namespace cinux::arch::per_cpu {

/// The scratch block one processor owns.
struct PerCpu {
    unsigned long       kernel_stack_top;  ///< Supervisor stack the next entry uses.
    unsigned long       user_stack_top;    ///< Where the last user drop put sp.
    cinux::proc::Task*  current = nullptr; ///< The task running on this core.
    unsigned long       cpu_id;            ///< The number this core reports.
    unsigned long       apic_id;           ///< Kept for the shared fields' shape.
    cinux::proc::Task*  pending_reap = nullptr;  ///< A dead task awaiting reclaim.
    unsigned long       slice_ticks;       ///< Ticks left before a preemption.
    unsigned long       preempt_depth;     ///< Nested preemption suppressions.
    unsigned long       no_reschedule_depth;  ///< Nested "do not switch" scopes.
    unsigned long       queue_depth;       ///< Ready tasks at the last rotate.
    bool                rotate_due;        ///< A rotate was asked for.
    bool                notify_pending;    ///< A wakeup is already in flight.
    std::atomic<unsigned int> local_timer_interrupts{0};
    std::atomic<unsigned int> reschedule_interrupts{0};
    unsigned long       current_slot;      ///< The mini-task; ~0 means the caller.

    /// The block installed on the executing processor.
    static PerCpu& self();
};

/// Points TPIDRPRW at a block; every processor calls this once.
inline void Install(PerCpu* block) {
    asm volatile("mcr p15, 0, %0, c13, c0, 4" : : "r"(block));
}

/// Follows TPIDRPRW to the executing processor's block.
inline PerCpu& self() {
    unsigned long base = 0;
    asm volatile("mrc p15, 0, %0, c13, c0, 4" : "=r"(base));
    return *reinterpret_cast<PerCpu*>(base);
}

inline PerCpu& PerCpu::self() {
    unsigned long base = 0;
    asm volatile("mrc p15, 0, %0, c13, c0, 4" : "=r"(base));
    return *reinterpret_cast<PerCpu*>(base);
}

}  // namespace cinux::arch::per_cpu
