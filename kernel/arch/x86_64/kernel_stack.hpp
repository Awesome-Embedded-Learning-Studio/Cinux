/**
 * @file    kernel_stack.hpp
 * @brief   The ring-0 stack an entry into the kernel lands on.
 *
 * A scheduler switching away from a task must tell the machine where the
 * next ring-0 entry should land. On x86-64 that slot lives in the TSS; on
 * another port it may be a scratch register or nothing at all.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/x86_64/tss.hpp"

namespace cinux::arch {

/**
 * @brief         Names the stack the next ring-0 entry should use.
 * @param[in]     top   Stack top for this CPU.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline void SetKernelStackTop(unsigned long top) {
    tss::Tss::self().rsp0 = top;
}

/**
 * @brief         Reads the kernel stack pointer this CPU is running on.
 * @return        The live stack pointer.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long long CurrentKernelStack() {
    unsigned long long rsp = 0;  // NOLINT(misc-const-correctness) written by the asm output
    asm volatile("movq %%rsp, %0" : "=r"(rsp));
    return rsp;
}

}  // namespace cinux::arch
