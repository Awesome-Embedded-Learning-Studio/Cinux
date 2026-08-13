/**
 * @file kernel/proc/process_internal.hpp
 * @brief Shared internal state for process sub-modules
 *
 * Declares the TID counter, stack-virtual-address allocator, and the CoW
 * page-table copier shared between task_builder.cpp, fork.cpp, and clone.cpp.
 * Not part of the public API.
 */

#pragma once

#include <stdint.h>

#include "kernel/lib/atomic.hpp"
#include "kernel/mm/vma.hpp"  // VMAStore (copy_page_table_level MAP_SHARED CoW decision)
#include "kernel/proc/process.hpp"

namespace cinux::proc {

extern cinux::lib::Atomic<uint64_t> next_tid;

enum class ChildWaitAction : uint8_t {
    kReap,
    kSleep,
    kNoChildren,
    kNotFound,
    kNotExited,
};

struct ChildWaitDecision {
    ChildWaitAction action;
    Task*           target;
};

/**
 * @brief Atomically scan children or publish the caller's blocked state
 *
 * Serialises waitpid's condition check with publish_child_exit(), closing the
 * cross-CPU window where an exiting child could wake a still-Running parent
 * immediately before the parent marked itself Blocked.
 */
ChildWaitDecision prepare_child_wait(Task* parent, int pid, bool no_hang);

/**
 * @brief Publish a child exit and wake its waitpid parent
 *
 * Publishes exit_status + Zombie under the same handshake lock used by
 * prepare_child_wait(), then sends SIGCHLD and wakes the parent after releasing
 * that lock.
 */
void publish_child_exit(Task* task, int encoded_status);

/**
 * @brief Inherit process-group / session membership into a forked/cloned child
 *
 * Called after the child TCB is memcpy'd from the parent (F3-M3 batch 1).
 * A "root" fork (parent->pgid == 0, i.e. the parent is a kernel/bootstrap
 * task with no group) founds a new process group AND session -- the child
 * becomes its own leader (pgid == sid == child_pid).  Otherwise the child
 * inherits the parent's group, session, session-leader pointer, and
 * controlling terminal.  Centralised here so fork() and clone() share one
 * testable rule instead of relying on the implicit memcpy copy.
 *
 * @param child     Child task (already memcpy'd from parent)
 * @param parent    Parent task
 * @param child_pid PID assigned to the child
 */
void inherit_process_identity(Task* child, const Task* parent, int child_pid);

uint64_t alloc_stack_vaddr(uint64_t pages);

/**
 * @brief Allocate and map a guarded task-owned hardware-IRQ stack
 *
 * The stack is separate from the syscall/kernel stack because an IRQ-exit
 * context may be suspended while another task runs.  Context switching updates
 * IST2 to the incoming task's irq_stack_top.
 */
bool alloc_irq_stack(Task* task);

/**
 * @brief Size of the syscall_entry pt_regs frame (16 qwords: user_rsp..rbp)
 *
 * It sits at the very top of the kernel stack
 * ([kernel_stack_top-128, kernel_stack_top)) because syscall_entry loads RSP
 * from %gs:0 == kernel_stack_top.  Shared by the user-fork/clone child setup
 * (which copies it onto the child's clean stack) and clone()'s user-RSP patch.
 */
constexpr uint64_t kSyscallFrameSize = 128;

struct KernelForkCalleeRegs {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbx;
};

inline KernelForkCalleeRegs capture_kernel_fork_callee_regs() {
    KernelForkCalleeRegs regs{};
    __asm__ volatile(
        "movq %%r15, %0\n\t"
        "movq %%r14, %1\n\t"
        "movq %%r13, %2\n\t"
        "movq %%r12, %3\n\t"
        "movq %%rbx, %4"
        : "=m"(regs.r15), "=m"(regs.r14), "=m"(regs.r13), "=m"(regs.r12), "=m"(regs.rbx)
        :
        : "memory");
    return regs;
}

// Recursively copy a page-table level for CoW fork/clone (fork.cpp). @p virt_base
// is the VA of the table's entry 0 (PML4 caller passes i<<39); @p vmas is the
// parent's VMA list -- at the leaf, MAP_SHARED entries stay writable (no CoW)
// so writes hit the real shared page.
void copy_page_table_level(uint64_t src_phys, uint64_t dst_phys, int level, uint64_t virt_base,
                           cinux::mm::IVMAStore& vmas);

/**
 * @brief Set up a USER fork/clone child to resume via ret_from_fork
 *
 * Used when the caller has a user address space (parent->addr_space != nullptr):
 * the production shell command-fork path (sys_fork) and user pthreads (sys_clone).
 * The child gets a CLEAN kernel stack -- only the parent's 128-byte syscall
 * pt_regs frame is copied to the top -- and ctx.rip = ret_from_fork.  When first
 * scheduled the child reads user RIP/RSP/RFLAGS + syscall-preserved registers
 * out of that frame and SYSRETQs to Ring 3 with rax=0, exactly like
 * syscall_entry's return tail.
 *
 * This is the Linux ret_from_fork style: the child does NOT run on a copy of the
 * parent's kernel stack and does NOT unwind a copied RBP chain, so it is immune
 * to the gcc-13 -O2+ubsan frame-layout fragility that broke the old approach.
 *
 * @param child                  Child task (kernel stack already allocated/mapped)
 * @param parent_kernel_stack_top  Parent's kernel_stack_top (frame lives at top-128)
 */
void prepare_user_fork_context(Task* child, uint64_t parent_kernel_stack_top);

/**
 * @brief Set up a KERNEL fork/clone child to resume the kernel caller
 *
 * Used when the caller has no user address space (parent->addr_space == nullptr):
 * a kernel/bootstrap thread forking (the shell-launch and ring-3 smoke path,
 * where the child later installs its own address space and launch_user_program's).
 * The parent's used kernel stack is copied so the caller's frame (and locals) are
 * preserved; the child resumes at the fork/clone return address with rax=0 via
 * fork_child_trampoline (xor rax,rax; ret).
 *
 * @p parent_frame_base is the fork/clone call frame base, captured with
 * __builtin_frame_address(0) -- the compiler-tracked frame address, robust where a
 * raw `movq %rbp` is not (the gcc-13 -O2+ubsan + per-function-optimize pitfall).
 * The return address lives at [frame_base+8], the saved caller rbp at [frame_base]
 * (SysV frame layout; the kernel is built globally with -fno-omit-frame-pointer).
 *
 * Unlike the deleted prepare_copied_kernel_stack_context, this does NOT walk and
 * rewrite the copied RBP chain: the kernel child runs forward (e.g. into
 * launch_user_program, which does not return) and never unwinds back up through
 * the copied frames, so only its own immediate caller frame needs a valid rbp.
 */
void prepare_kernel_fork_context(Task* child, uint64_t parent_stack_start,
                                 uint64_t parent_stack_top, uint64_t child_stack_start,
                                 uint64_t                    parent_frame_base,
                                 const KernelForkCalleeRegs& caller_regs);

/**
 * @brief Free a task's kernel and hardware-IRQ stacks
 *
 * Defined in process_new.cpp; used by waitpid reap (Q4e-2) and the scheduler's
 * deferred-free reaper (Q4e-3). Recovers phys via translate, unmaps each page,
 * frees each physical block. Safe only when the task is NOT running on either
 * stack.
 */
void free_kernel_stack(Task* task);

}  // namespace cinux::proc
