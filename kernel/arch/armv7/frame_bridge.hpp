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
 *                so signal delivery and sigreturn preserve every user
 *                register, including the separate user LR bank.
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
    // Pack the banked LR and r7 in one neutral callee-saved seat.
    frame.rbp = trap.user_lr | (static_cast<unsigned long long>(trap.r7) << 32);
    frame.r12 = trap.r8 | (static_cast<unsigned long long>(trap.r9) << 32);
    frame.r13 = trap.r10 | (static_cast<unsigned long long>(trap.r11) << 32);
    frame.r14 = trap.r12;
}

/**
 * @brief         Writes the neutral frame's verdict back into the trap.
 * @param[in]     frame  The neutral seating after the handler ran.
 * @param[out]    trap   The trap the stub will return through.
 * @return        None.
 * @note          The answer, PC, stack and flags return through the neutral
 *                seats; the packed callee-saved seats restore the full
 *                register image on sigreturn.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline void Store(const cinux::syscall::SyscallFrame& frame, UserTrap& trap) {
    trap.r0 = static_cast<unsigned long>(frame.rax);
    trap.r1 = static_cast<unsigned long>(frame.rsi);
    trap.r2 = static_cast<unsigned long>(frame.rdx);
    trap.r3 = static_cast<unsigned long>(frame.r10);
    trap.r4 = static_cast<unsigned long>(frame.r8);
    trap.r5 = static_cast<unsigned long>(frame.r9);
    trap.r6 = static_cast<unsigned long>(frame.rbx);
    trap.user_lr = static_cast<unsigned long>(frame.rbp);
    trap.r7 = static_cast<unsigned long>(frame.rbp >> 32);
    trap.r8 = static_cast<unsigned long>(frame.r12);
    trap.r9 = static_cast<unsigned long>(frame.r12 >> 32);
    trap.r10 = static_cast<unsigned long>(frame.r13);
    trap.r11 = static_cast<unsigned long>(frame.r13 >> 32);
    trap.r12 = static_cast<unsigned long>(frame.r14);
    // The exec rewrite: the builder zeroed the frame and seated the
    // new entry, stack, and answer. Load seeded those very seats from
    // this trap, so writing them back is a lossless round trip for
    // every ordinary call and the new world's doorway for exec. The
    // flags seat holds the caller's exact SPSR on an ordinary call,
    // and a fresh user CPSR on exec. In particular, a Thumb caller
    // execing an ARM image must lose its old T/IT/J state.
    // An odd resume address is the arm way of spelling Thumb code: the
    // bit is a state marker, not part of the instruction address, so
    // the program counter takes the even word and the state moves into
    // the SPSR's T bit. A normal call seeded the same value from the
    // trap, including its T bit, so Thumb calls with even saved PCs
    // retain their instruction state too.
    unsigned long kResume = static_cast<unsigned long>(frame.rcx);
    trap.spsr = static_cast<unsigned long>(frame.r11);
    if ((kResume & 1UL) != 0UL) {
        trap.spsr |= 0x20UL;
        kResume &= ~1UL;
    }
    trap.lr = kResume;
    trap.sp = static_cast<unsigned long>(frame.user_rsp);
}

}  // namespace cinux::arch::frame
