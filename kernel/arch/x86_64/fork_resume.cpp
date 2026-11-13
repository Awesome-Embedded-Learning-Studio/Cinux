/**
 * @file    fork_resume.cpp
 * @brief    The x86_64 half of a fork: how the child first resumes.
 *
 * The parent's syscall entry frame is copied to the child's kernel
 * stack top and the child's context is pointed at ForkChildEntry, the
 * mirror of the syscall return tail that leaves for ring 3 through
 * sysretq with rax zero - the whole difference between parent and
 * child from the caller's chair.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/proc/fork.hpp"

#include "cinux/memory.hpp"
#include "cinux/ptr.hpp"
#include "kernel/proc/proc_config.hpp"
#include "kernel/proc/task.hpp"
#include "kernel/syscall/syscall.hpp"

extern "C" {

/**
 * @brief         The child side of a fork, entered only once per fork.
 * @note          Unwinds the entry frame synthesized on the child's
 *                kernel stack and leaves for ring 3 through sysretq
 *                with rax zero.
 * @since         0.1.0
 * @ingroup       kernel_proc
 */
void ForkChildEntry();
}

namespace cinux::proc {

void PrepareForkResume(Task& child, const Task& parent, unsigned long long frame_address,
                       unsigned long long stack_top) {
    const auto& parent_frame = *cinux::base::PtrAt<const syscall::SyscallFrame>(frame_address);
    child.ctx                = parent.ctx;
    child.ctx.fpu            = parent.entry_fpu;
    child.ctx.r12            = parent_frame.r12;
    child.ctx.r13            = parent_frame.r13;
    child.ctx.r14            = parent_frame.r14;
    child.ctx.r15            = parent_frame.r15;
    child.ctx.rip            = reinterpret_cast<unsigned long long>(&ForkChildEntry);
    child.ctx.rsp            = stack_top - sizeof(syscall::SyscallFrame);
    cinux::base::CopyBytes(cinux::base::PtrAt<void>(child.ctx.rsp),
                           cinux::base::PtrAt<const void>(frame_address),
                           sizeof(syscall::SyscallFrame));
}

}  // namespace cinux::proc
