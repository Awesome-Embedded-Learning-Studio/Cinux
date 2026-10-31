#include "kernel/syscall/sys_flock.hpp"

#include "cinux/result.hpp"
#include "cinux/shared.hpp"
#include "kernel/fs/file_lock.hpp"
#include "kernel/fs/file_table.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/signals.hpp"
#include "kernel/proc/task.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/syscall_internal.hpp"

namespace cinux::syscall {

long long HandleFlock(const SyscallFrame& frame) {
    if (frame.rdi >= fs::kFileTableMax) {
        return -kEbadf;
    }
    const auto kOperation                                = static_cast<unsigned long>(frame.rsi);
    const bool kNoBlock                                  = (kOperation & fs::kFlockNb) != 0;
    const base::Result<base::Shared<fs::OpenFile>> kLive = CurrentTask()->files->get(frame.rdi);
    if (!kLive.ok()) {
        return -kEbadf;
    }
    fs::FileLocks& locks = fs::FileLocks::self();
    for (;;) {
        const base::Result<void> kAttempt =
            locks.attempt(*kLive.value(), kOperation & ~fs::kFlockNb);
        if (kAttempt.ok()) {
            return 0;
        }
        if (kAttempt.error() != base::KernelError::kWouldBlock) {
            return -ErrnoFrom(kAttempt.error());
        }
        if (kNoBlock) {
            return -kEagain;
        }
        const proc::Task* const kSelf = CurrentTask();
        if (proc::HasPendingSignal(*kSelf)) {
            return -kEintr;
        }
        proc::Scheduler::self().yield();
    }
}

}  // namespace cinux::syscall
