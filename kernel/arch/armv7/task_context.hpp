/**
 * @file    task_context.hpp
 * @brief   Arming a kernel task's first context on armv7.
 *
 * The portable PrepareTaskContext keeps the stack magic; the register
 * image is the port's: a stack, a return address, a mode, and two words
 * of payload for the trampoline (body and exit hook).
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/armv7/context.hpp"

namespace cinux::arch {

/**
 * @brief         Writes the armv7 start image into a task context.
 * @param[out]    ctx          Zeroed by this call, then armed.
 * @param[in]     trampoline   First instruction the task runs.
 * @param[in]     entry        Body; goes to r4, read by TaskTrampoline.
 * @param[in]     exit_hook    Goes to r5; a returning body jumps there.
 * @param[in]     stack_bottom Lowest stack byte (unused here).
 * @param[in]     stack_top    First sp, 8-byte aligned.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline void ArmTaskContext(CpuContext& ctx, unsigned long trampoline, unsigned long entry,
                           unsigned long exit_hook, unsigned long /*stack_bottom*/,
                           unsigned long stack_top) {
    ctx      = CpuContext{};
    ctx.r4   = entry;        // body, read by TaskTrampoline
    ctx.r5   = exit_hook;    // where a returning body lands
    ctx.sp   = stack_top & ~7UL;
    ctx.lr   = trampoline;
    ctx.spsr = 0x13UL;  // supervisor mode
}

}  // namespace cinux::arch
