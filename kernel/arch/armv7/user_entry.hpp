/**
 * @file    user_entry.hpp
 * @brief   The armv7 drop into user mode, with a return point to come back.
 *
 * x86 enters ring 3 through an IRETQ frame that carries rip, rsp and
 * rflags together. armv7 has no such frame: the mode is a CPSR field,
 * and the user stack is a banked register. So the drop is three moves:
 * switch to user mode, set sp while there, branch. The kernel's own
 * registers and return address are saved first, so an svc-exit can
 * resume the caller as if this were a longjmp.
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
 * @brief         Drops to user mode at entry on the given user stack.
 * @param[in]     entry        User-mode entry address.
 * @param[in]     stack        User stack top, 8-byte aligned.
 * @param[out]    kernel_ctx   Receives the kernel context to resume on exit.
 * @note          Implemented in user_entry.S so its lr is the caller's
 *                return address at entry; an inlined version captured
 *                whatever the previous call left in lr. Does not return
 *                normally; the svc-exit path resumes the caller through
 *                kernel_ctx.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
extern "C" void EnterUserModeWithReturn(unsigned long entry, unsigned long stack,
                                        CpuContext* kernel_ctx);

/**
 * @brief         The neutral door: enter user mode, resume here on exit.
 * @param[in]     entry   User-mode entry address.
 * @param[in]     stack   User stack top, 8-byte aligned.
 * @note          The x86 spelling leaves ring 0 through an IRETQ frame
 *                and never returns; this port has no such frame, so the
 *                caller's context parks in this very frame and the
 *                svc-exit path resumes it -- the task then returns the
 *                way a kernel task always does. Same contract at the
 *                call site: behind this call the caller's frame is the
 *                parked kernel half.
 */
inline void EnterUserMode(unsigned long entry, unsigned long stack) {
    CpuContext parked{};
    EnterUserModeWithReturn(entry, stack, &parked);
}

}  // namespace cinux::arch
