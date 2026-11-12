/**
 * @file    proc_shim.cpp
 * @brief   Link shim: the four Scheduler members the shared sync/lock
 *          code references before armv7 adopts the real scheduler.
 *
 * The fs layer's spinlocks compile unchanged from kernel/proc/sync.cpp,
 * but that file's holder bookkeeping asks Scheduler::current() and its
 * sleeping locks (Mutex, Semaphore) ask for the blocking path. armv7
 * still runs its own mini scheduler, so until the generic one moves
 * over (the planned second step), current() answers with one static
 * stand-in task standing for the main line, and every blocking entry
 * refuses loudly instead of failing silently: if anything on the
 * file-layer path ever tried to sleep, the kernel halts with a named
 * assertion rather than corrupting state.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/proc/scheduler.hpp"

#include "cinux/assert.hpp"
#include "kernel/proc/task.hpp"

namespace cinux::proc {

/// One stand-in task standing for the armv7 main line.
/// Spinlock holder checks only compare pointers, so a single stable
/// address keeps lock/unlock pairs consistent; its name shows up in
/// dumps when holder_name() is asked.
namespace {
Task kArmMainStub{};
}  // namespace

Task* Scheduler::current() const {
    kArmMainStub.name = "armv7-main";
    return &kArmMainStub;
}

void Scheduler::prepare_to_wait() {
    base::safety::Check(false, "armv7: sleeping lock reached before the scheduler moved over");
}

void Scheduler::schedule_blocked() {
    base::safety::Check(false, "armv7: sleeping lock reached before the scheduler moved over");
}

void Scheduler::unblock(Task& /*task*/) {
    base::safety::Check(false, "armv7: sleeping lock reached before the scheduler moved over");
}

}  // namespace cinux::proc
