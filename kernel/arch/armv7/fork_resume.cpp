/**
 * @file    fork_resume.cpp
 * @brief    The armv7 half of a fork: how the child first resumes.
 *
 * The parent's episode frame is copied to the child's kernel stack top
 * and the child's context is pointed at SeatForkChildEntry, the mirror
 * of the seat body: it adopts its own seat state, leaves for user mode
 * through ResumeTrap with r0 zero, and parks like any seated process
 * from its first service call on.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "cinux/memory.hpp"
#include "cinux/ptr.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/arch/context.hpp"
#include "kernel/proc/fork.hpp"
#include "kernel/proc/task.hpp"

namespace cinux::arch {
extern "C" cinux::arch::UserTrap* g_current_trap;
}

extern "C" void SeatForkChildEntry();

namespace cinux::proc {

void PrepareForkResume(Task& child, const Task& parent, unsigned long long frame_address,
                       unsigned long long stack_top) {
    // The neutral picture cannot carry this port's resume words (the
    // user pc rides the banked lr, r8-r11 have no neutral seat), so the
    // port's own episode frame is the authoritative copy; the parent is
    // mid-service in it right now.
    static_cast<void>(parent);
    static_cast<void>(frame_address);
    const unsigned long kFrame = static_cast<unsigned long>(stack_top) - sizeof(cinux::arch::UserTrap);
    auto* const kImage = cinux::base::PtrAt<cinux::arch::UserTrap>(kFrame);
    cinux::base::CopyBytes(kImage, cinux::arch::g_current_trap, sizeof(cinux::arch::UserTrap));
    kImage->r0 = 0;  // the child's fork answer
    child.ctx      = arch::CpuContext{};
    child.ctx.sp   = kFrame;
    child.ctx.lr   = reinterpret_cast<unsigned long>(&SeatForkChildEntry);
    child.ctx.spsr = 0x13UL;  // supervisor: the entry runs as a kernel body
    // The thread pointer is a register, not memory: a fork's child has
    // to be handed the parent's, or its first errno write lands at
    // TP=0 and the child dies before it can exec.
    asm volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(child.ctx.tpidruro));
}

}  // namespace cinux::proc
