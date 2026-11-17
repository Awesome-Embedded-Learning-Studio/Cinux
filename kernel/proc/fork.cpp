#include "kernel/proc/fork.hpp"

#include <utility>

#include "cinux/addr.hpp"
#include "cinux/assert.hpp"
#include "cinux/memory.hpp"
#include "cinux/own.hpp"
#include "cinux/ptr.hpp"
#include "cinux/shared.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/fs/file_table.hpp"
#include "kernel/fs/fs_config.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/cow.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/proc/proc_config.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/arch/kernel_stack.hpp"
#include "kernel/proc/task.hpp"
#include "kernel/syscall/signal.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::proc {

namespace {

/// A spawn refused before it starts beats one that dies halfway down
/// the kernel stack: the spawner's own call chain must sit well inside
/// the stack it is about to hand away, or the frames it touches while
/// assembling the child can run off the end.
void guard_spawn_depth(const Task& spawner) {
    const unsigned long long kUsed =
        spawner.stack_base + kStackBytes - cinux::arch::CurrentKernelStack();
    cinux::base::safety::Check(kUsed < (kStackBytes / 4) * 3,
                               "spawn refused: the caller's kernel stack is nearly spent");
}

}  // namespace

Task* BuildForkChild(unsigned long long frame_address) {
    const arch::IrqGuard kEntryGuard;
    const auto&              scheduler = Scheduler::self();
    Task* const              kParent   = scheduler.current();
    cinux::base::safety::Check(kParent != nullptr, "fork outside any task");
    guard_spawn_depth(*kParent);
    cinux::base::safety::Check(kParent->space.holds(), "fork of a task without a world");
    base::Shared<mm::AddressSpace> space(
        mm::cow::CloneSharedSpace(*kParent->space, kParent->vmas.get()));
    if (!space.holds()) {
        return nullptr;
    }
    cinux::base::safety::Check(kParent->vmas.holds(), "fork parent lost its range register");
    base::Shared<mm::VmaStore> vmas(kParent->vmas->clone());
    if (!vmas.holds()) {
        return nullptr;
    }
    const cinux::base::PhysAddr kStack = mm::Pmm::self().allocate_pages(kStackOrder);
    if (kStack == cinux::base::PhysAddr{}) {
        return nullptr;
    }
    base::Own<Task> child = base::MakeOwn<Task>();
    if (!child.holds()) {
        mm::Pmm::self().free_pages(kStack, kStackOrder);
        return nullptr;
    }
    Task* const kBorn = child.get();
    // The first resume is the port's business: how a fresh task returns
    // to the parent's ring-3 chair with the fork result zeroed. The
    // stack top hands the arch its frame staging room.
    const unsigned long kStackVirt = mm::DirectMapVirt(kStack.raw);
    PrepareForkResume(*kBorn, *kParent, frame_address, kStackVirt + kStackBytes);
    kBorn->state      = TaskState::kReady;
    kBorn->name       = kParent->name;
    kBorn->stack_base = kStackVirt;
    kBorn->user_root  = space->root();
    kBorn->space      = std::move(space);
    kBorn->vmas       = std::move(vmas);
    kBorn->parent     = kParent;
    kBorn->signals    = CloneSignalState(*kParent->signals);
    if (!kBorn->signals.holds()) {
        mm::Pmm::self().free_pages(kStack, kStackOrder);
        return nullptr;
    }
    kBorn->pgid   = kParent->pgid;
    kBorn->sid    = kParent->sid;
    kBorn->fs_ctx = fs::CloneFsContext(*kParent->fs_ctx);
    if (!kBorn->fs_ctx.holds()) {
        mm::Pmm::self().free_pages(kStack, kStackOrder);
        return nullptr;
    }
    cinux::base::CopyBytes(kBorn->exe, kParent->exe, fs::kFsPathMax);
    kBorn->controlling_tty = kParent->controlling_tty;
    cinux::base::CopyBytes(kBorn->comm, kParent->comm, sizeof(kBorn->comm));
    cinux::base::CopyBytes(kBorn->limits, kParent->limits, sizeof(kBorn->limits));
    kBorn->uid               = kParent->uid;
    kBorn->euid              = kParent->euid;
    kBorn->gid               = kParent->gid;
    kBorn->egid              = kParent->egid;
    kBorn->keep_capabilities = kParent->keep_capabilities;
    base::CopyBytes(kBorn->supplementary_groups, kParent->supplementary_groups,
                    sizeof(kBorn->supplementary_groups));
    kBorn->supplementary_group_count = kParent->supplementary_group_count;
    kBorn->files                     = kParent->files->clone();
    if (!kBorn->files.holds()) {
        mm::Pmm::self().free_pages(kStack, kStackOrder);
        return nullptr;
    }
    return child.release();
}

}  // namespace cinux::proc

extern "C" void FinishForkChild() {
    auto* const kSelf = cinux::proc::Scheduler::self().current();
    if (kSelf->set_child_tid != 0) {
        const auto kTid = static_cast<unsigned int>(kSelf->pid);
        if (!cinux::syscall::uaccess::CopyToUser(kSelf->set_child_tid, &kTid, sizeof(kTid))) {
            cinux::proc::Scheduler::self().exit_current(cinux::syscall::kSigSegv);
        }
        kSelf->set_child_tid = 0;
    }
}
