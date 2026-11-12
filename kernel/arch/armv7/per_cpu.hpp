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

namespace cinux::arch::per_cpu {

/// The scratch block one processor owns.
struct PerCpu {
    unsigned long cpu_id;
    unsigned long kernel_stack_top;
    unsigned long user_stack_top;
    unsigned long pending_reap;
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

}  // namespace cinux::arch::per_cpu
