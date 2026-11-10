#include "kernel/syscall/sys_clone.hpp"

#include "kernel/arch/x86_64/context.hpp"
#include "kernel/proc/clone.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/sync.hpp"
#include "kernel/proc/task.hpp"
#include "kernel/syscall/signal.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/syscall_internal.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::syscall {

namespace {

/// Every flag bit this handler understands; anything outside the mask
/// is refused whole rather than half-served. CLONE_SYSVSEM is accepted
/// as a declaration only: there is no SysV semaphore table yet, so no
/// undo list is shared or copied. Revisit this comment when semaphores
/// land; the flag then has to mean something.
constexpr unsigned long long kServedFlags =
    kCloneVfork | kCloneVm | kCloneFs | kCloneFiles | kCloneSighand | kCloneThread | kCloneSysVsem |
    kCloneSetTls | kCloneParentSetTid | kCloneChildClearTid | kCloneChildSetTid | 0xFFULL;

}  // namespace

long long HandleClone(const SyscallFrame& frame) {
    const unsigned long long kFlags = frame.rdi;
    if ((kFlags & ~kServedFlags) != 0) {
        return -kEinval;
    }
    // Shared hands need a shared world: Linux rejects these mixes with
    // EINVAL, and the retire paths below lean on the promise they make.
    // A CLONE_THREAD member without CLONE_VM would exit through the
    // "different address space" branch and lose its join word's wake;
    // a CLONE_SIGHAND without CLONE_VM would share signal actions with
    // a world the table's words cannot reach.
    if ((kFlags & kCloneSighand) != 0 && (kFlags & kCloneVm) == 0) {
        return -kEinval;
    }
    if ((kFlags & kCloneThread) != 0 &&
        (kFlags & (kCloneVm | kCloneSighand)) != (kCloneVm | kCloneSighand)) {
        return -kEinval;
    }
    const auto kExitSignal = kFlags & 0xFFULL;
    if (kExitSignal != 0 && (kExitSignal != kSigChld || (kFlags & kCloneThread) != 0)) {
        return -kEinval;
    }
    const unsigned long long kStack = frame.rsi;
    if (kStack >= kUserAddressLimit) {
        return -kEinval;
    }
    if ((kFlags & kCloneSetTls) != 0 && !arch::IsCanonicalAddress(frame.r8)) {
        return -kEinval;
    }
    const unsigned long long kParentTid = frame.rdx;
    const unsigned long long kChildTid  = frame.r10;
    if ((kFlags & kCloneParentSetTid) != 0 &&
        !UserRangeWritable(kParentTid, sizeof(unsigned int))) {
        return -kEfault;
    }
    if ((kFlags & (kCloneChildSetTid | kCloneChildClearTid)) != 0 &&
        !UserRangeWritable(kChildTid, sizeof(unsigned int))) {
        return -kEfault;
    }
    const proc::ClonePlan kPlan{
        .flags      = kFlags,
        .user_stack = kStack != 0 ? kStack : frame.user_rsp,
        .tls        = frame.r8,
    };
    proc::Task* const kChild =
        proc::BuildCloneChild(reinterpret_cast<unsigned long long>(&frame), kPlan);
    if (kChild == nullptr) {
        return -kEnomem;
    }
    if ((kFlags & kCloneChildClearTid) != 0) {
        kChild->clear_child_tid = kChildTid;
    }
    proc::Scheduler::self().seat(*kChild, false);
    if ((kFlags & kCloneThread) != 0) {
        kChild->tgid = CurrentTask()->tgid;
    }
    const auto kTid = static_cast<unsigned int>(kChild->pid);
    if ((kFlags & kCloneParentSetTid) != 0 &&
        !uaccess::CopyToUser(kParentTid, &kTid, sizeof(kTid))) {
        return -kEfault;
    }
    if ((kFlags & kCloneChildSetTid) != 0) {
        kChild->set_child_tid = kChildTid;
    }
    const auto       kPid = static_cast<long long>(kChild->pid);
    proc::Completion done;
    if ((kFlags & kCloneVfork) != 0) {
        kChild->vfork_done = &done;
    }
    proc::Scheduler::self().unblock(*kChild);
    if ((kFlags & kCloneVfork) != 0) {
        done.wait();
    }
    done.retire();
    return kPid;
}

}  // namespace cinux::syscall
