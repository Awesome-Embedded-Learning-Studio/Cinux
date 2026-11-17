/**
 * @file    seat_run.hpp
 * @brief   The per-task seat state both seat halves share.
 *
 * A seated user process carries one SeatRun through its task. The
 * production seat machinery (user_seat.cpp) and the self-check probe
 * (seat_probe.cpp) both read it, so the record and its accessor live
 * here rather than in either translation unit.
 *
 * @author  Charliechen114514
 * @date    2026-11-17
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/context.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/task.hpp"

namespace cinux::armv7 {

/// The per-task state a running user world lives in. The words sit on
/// the launcher's stack and outlive the body; the task's arch_seat
/// points here so every episode side finds the current process's own
/// world without a side registry.
struct SeatRun {
    mm::AddressSpace*        space = nullptr;
    mm::VmaStore*            ranges = nullptr;
    cinux::arch::CpuContext* body = nullptr;  // where the parked kernel half waits
    unsigned long            exit_status = 0;
    bool                     exited      = false;
    bool                     spoke       = false;
};

/// The current process's seat state, or null while none runs.
inline SeatRun* current_seat() {
    proc::Task* const kTask = proc::Scheduler::self().current();
    return kTask == nullptr ? nullptr : static_cast<SeatRun*>(kTask->arch_seat);
}

}  // namespace cinux::armv7
