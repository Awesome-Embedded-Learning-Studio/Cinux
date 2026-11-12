/**
 * @file    kernel_stack.hpp
 * @brief   The armv7 kernel stack top, tracked per CPU.
 *
 * x86-64 stores this in the TSS so a ring change lands on the right
 * stack. armv7 has no TSS: the supervisor stack is whatever SVC's SP
 * holds, so the top lives in the per-CPU block and a task switch will
 * swap it there.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/armv7/per_cpu.hpp"

namespace cinux::arch {

/// Names the stack top the next supervisor entry should use.
inline void SetKernelStackTop(unsigned long top) {
    per_cpu::self().kernel_stack_top = top;
}

/// Reads the kernel stack pointer this CPU is running on.
inline unsigned long long CurrentKernelStack() {
    unsigned long long sp = 0;
    asm volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

}  // namespace cinux::arch
