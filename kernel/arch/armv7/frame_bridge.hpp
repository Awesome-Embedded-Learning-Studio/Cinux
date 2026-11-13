/**
 * @file    frame_bridge.hpp
 * @brief   The armv7 service-call frame as the neutral syscall frame.
 *
 * The neutral syscall layer speaks one register picture; its names
 * came from the first port, but the seats are plain: the number, the
 * caller's stack, the return address, the flags, and six arguments.
 * This bridge maps the armv7 trap onto those seats and back, so the
 * neutral handlers serve armv7 processes without knowing the port.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/syscall/syscall.hpp"

namespace cinux::arch::frame {

/**
 * @brief         Loads the neutral frame picture from an armv7 trap.
 * @param[in]     trap   The saved service-call registers.
 * @param[out]    frame  Receives the neutral seating.
 * @return        None.
 * @note          The callee-saved seats carry the armv7 banked words
 *                so a round trip is lossless; the neutral layer never
 *                reads them.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline void Load(const UserTrap& trap, cinux::syscall::SyscallFrame& frame) {
    frame.rax      = trap.r7;
    frame.user_rsp = trap.sp;
    frame.rcx      = trap.lr;
    frame.r11      = trap.spsr;
    frame.rdi      = trap.r0;
    frame.rsi      = trap.r1;
    frame.rdx      = trap.r2;
    frame.r10      = trap.r3;
    frame.r8       = trap.r4;
    frame.r9       = trap.r5;
    frame.rbx      = trap.r6;
}

/**
 * @brief         Writes the neutral frame's verdict back into the trap.
 * @param[in]     frame  The neutral seating after the handler ran.
 * @param[out]    trap   The trap the stub will return through.
 * @return        None.
 * @note          Only the answer seat is written today; the armv7
 *                return path keeps the caller's own registers.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline void Store(const cinux::syscall::SyscallFrame& frame, UserTrap& trap) {
    trap.r0 = static_cast<unsigned long>(frame.rax);
}

}  // namespace cinux::arch::frame
