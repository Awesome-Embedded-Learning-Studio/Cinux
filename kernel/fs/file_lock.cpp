#include "kernel/fs/file_lock.hpp"

#include <atomic>

#include "cinux/assert.hpp"
#include "cinux/result.hpp"
#include "kernel/fs/open_file.hpp"

namespace cinux::fs {

bool FileLocks::names_node(const Held& held, const OpenFile& file) {
    return held.backend == file.backend && held.ino == file.node.ino;
}

void FileLocks::drop_held(const OpenFile& file) {
    for (;;) {
        const unsigned long kSlot =
            held_.find_if([&file](const Held& held) { return held.owner == &file; });
        if (kSlot == kFileLockMax) {
            return;
        }
        base::safety::Check(held_.erase(kSlot), "file lock slot vanished");
    }
}

void FileLocks::arm_final_close(OpenFile& file) {
    const unsigned long kArmed =
        hooks_.find_if([&file](const Hook& hook) { return hook.file == &file; });
    unsigned long slot = kArmed;
    if (slot == kLockHookMax) {
        slot = hooks_.insert(Hook{.file = &file, .previous = nullptr});
        if (slot == kLockHookMax) {
            return;
        }
    }
    Hook& hook                        = *hooks_.get(slot);
    void (*observed)(const OpenFile*) = file.on_last_close.load(std::memory_order_relaxed);
    for (;;) {
        hook.previous = observed;
        if (file.on_last_close.compare_exchange_weak(observed, &FileLocks::final_close,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed)) {
            return;
        }
    }
}

void FileLocks::final_close(const OpenFile* file) {
    FileLocks& self                   = FileLocks::self();
    void (*previous)(const OpenFile*) = nullptr;
    {
        const auto kGuard = self.gate_.guard();
        self.drop_held(*file);
        const unsigned long kSlot =
            self.hooks_.find_if([file](const Hook& hook) { return hook.file == file; });
        if (kSlot != kLockHookMax) {
            previous = self.hooks_.get(kSlot)->previous;
            base::safety::Check(self.hooks_.erase(kSlot), "file lock hook slot vanished");
        }
    }
    if (previous != nullptr) {
        previous(file);
    }
}

base::Result<void> FileLocks::attempt(OpenFile& file, unsigned long operation) {
    const unsigned long kWord = operation & (kFlockSh | kFlockEx | kFlockUn);
    if (kWord != kFlockSh && kWord != kFlockEx && kWord != kFlockUn) {
        return base::KernelError::kInvalidArgument;
    }
    const auto kGuard = gate_.guard();
    if (kWord == kFlockUn) {
        drop_held(file);
        return {};
    }
    const bool          kExclusive = kWord == kFlockEx;
    const unsigned long kBlocked   = held_.find_if([&](const Held& held) {
        return names_node(held, file) && held.owner != &file && (kExclusive || held.exclusive);
    });
    if (kBlocked != kFileLockMax) {
        return base::KernelError::kWouldBlock;
    }
    const unsigned long kFree =
        hooks_.find_if([&file](const Hook& hook) { return hook.file == &file; });
    unsigned long hook = kFree;
    if (hook == kLockHookMax) {
        hook = hooks_.insert(Hook{.file = &file, .previous = nullptr});
        if (hook == kLockHookMax) {
            return base::KernelError::kOutOfMemory;
        }
    }
    drop_held(file);
    if (held_.insert(Held{
            .backend   = file.backend,
            .ino       = file.node.ino,
            .owner     = &file,
            .exclusive = kExclusive,
        }) == kFileLockMax) {
        if (kFree == kLockHookMax) {
            base::safety::Check(hooks_.erase(hook), "file lock hook slot vanished");
        }
        return base::KernelError::kOutOfMemory;
    }
    arm_final_close(file);
    return {};
}

void FileLocks::release(OpenFile& file) {
    const auto kGuard = gate_.guard();
    drop_held(file);
}

}  // namespace cinux::fs
