#pragma once

#include "kernel/arch/armv7/frame_bridge.hpp"

namespace cinux::arch {

// Linux ARM rt_sigframe: siginfo, ucontext (including the reserved
// coprocessor area), and two return instructions. No x86 return slot.
struct ArmSignalFrame {
    unsigned int info[32]{};
    unsigned int flags{}, link{}, stack[3]{};
    unsigned int trap_no{}, error_code{}, oldmask{};
    unsigned int regs[18]{};  // r0-r15, CPSR, fault address
    unsigned int mask[32]{};
    alignas(8) unsigned int regspace[128]{};
    unsigned int retcode[2]{};
};
static_assert(__builtin_offsetof(ArmSignalFrame, regs) == 160);
static_assert(__builtin_offsetof(ArmSignalFrame, mask) == 232);
static_assert(sizeof(ArmSignalFrame) == 880);

inline void SaveSignalRegisters(const syscall::SyscallFrame& frame, ArmSignalFrame& image) {
    UserTrap trap{};
    frame::Store(frame, trap);
    const unsigned long* words = &trap.r0;
    for (unsigned int i = 0; i < 13; ++i) image.regs[i] = words[i];
    image.regs[13] = trap.sp;
    image.regs[14] = trap.user_lr;
    image.regs[15] = trap.lr;
    image.regs[16] = trap.spsr;
}

inline void RestoreSignalRegisters(const ArmSignalFrame& image, syscall::SyscallFrame& frame) {
    UserTrap trap{};
    unsigned long* words = &trap.r0;
    for (unsigned int i = 0; i < 13; ++i) words[i] = image.regs[i];
    trap.sp = image.regs[13];
    trap.user_lr = image.regs[14];
    trap.lr = image.regs[15];
    trap.spsr = image.regs[16];
    frame::Load(trap, frame);
    frame.rax = trap.r0;
}

}  // namespace cinux::arch
