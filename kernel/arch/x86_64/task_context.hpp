/**
 * @file    task_context.hpp
 * @brief   Arming a kernel task's first context on x86-64.
 *
 * The shared PrepareTaskContext keeps the portable parts (the stack magic)
 * and delegates the register image to the port. Here that image is the
 * iret frame's starting point: rsp, rip, r12 (the entry) and r13 (the
 * exit hook), plus the clean x87/SSE state a task must start with.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/memory.hpp"
#include "kernel/arch/x86_64/context.hpp"

namespace cinux::arch {

namespace task_context_detail {

constexpr unsigned long long kX87ControlWord = 0x037FULL;
constexpr unsigned long long kMxcsrDefault   = 0x1F80ULL;

/// A task must start with a defined x87/SSE state, not whatever the
/// previous occupant of the registers left behind.
inline void PrepareCleanFpuImage(CpuContext& ctx) {
    cinux::base::SetBytes(&ctx.fpu, 0, sizeof(ctx.fpu));
    auto* const kFcw   = reinterpret_cast<unsigned short*>(&ctx.fpu.words[0]);
    kFcw[0]            = static_cast<unsigned short>(kX87ControlWord);
    auto* const kMxcsr = reinterpret_cast<unsigned int*>(&ctx.fpu.words[3]);
    kMxcsr[0]          = static_cast<unsigned int>(kMxcsrDefault);
}

}  // namespace task_context_detail

/**
 * @brief         Writes the x86-64 start image into a task context.
 * @param[out]    ctx          Zeroed by this call, then armed.
 * @param[in]     trampoline   First instruction the task runs.
 * @param[in]     entry        Task body; goes to r12.
 * @param[in]     exit_hook    Where a returning body lands; goes to r13.
 * @param[in]     stack_bottom Lowest stack byte (unused here).
 * @param[in]     stack_top    First rsp, 16-byte aligned.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline void ArmTaskContext(CpuContext& ctx, unsigned long long trampoline,
                           unsigned long long entry, unsigned long long exit_hook,
                           unsigned long long /*stack_bottom*/, unsigned long long stack_top) {
    cinux::base::SetBytes(&ctx, 0, sizeof(ctx));
    task_context_detail::PrepareCleanFpuImage(ctx);
    ctx.rsp = stack_top;
    ctx.rip = trampoline;
    ctx.r12 = entry;
    ctx.r13 = exit_hook;
}

}  // namespace cinux::arch
