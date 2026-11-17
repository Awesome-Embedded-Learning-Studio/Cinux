/**
 * @file    user_seat.cpp
 * @brief   User processes seated on the real scheduler: the armv7
 *          shape of the x86 model where a Task carries its user world.
 *
 * A seated process is a real proc::Task whose body drops to user mode
 * on its own stack. Service calls run on that stack; the console faces
 * answer through the task's own FileTable (the seat bridge reads it
 * before the bring-up round-robin's table), and the exit service parks
 * the user frame and switches the body back through ContextSwitch's
 * exception return — no new assembly, the SPSR decides the world.
 *
 * The self-check probe that rides this seat lives in seat_probe.cpp and
 * compiles only into the self-check image; this file keeps the machinery
 * every seated process uses.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.2.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/user_seat.hpp"
#include "kernel/arch/armv7/seat_run.hpp"
#include "kernel/arch/armv7/cow_repair.hpp"

#include "kernel/arch/space_control.hpp"
#include "kernel/arch/armv7/per_cpu.hpp"
#include "kernel/mm/pmm.hpp"

#include "kernel/arch/armv7/context.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/user_entry.hpp"
#include "kernel/arch/armv7/user_fs.hpp"
#include "kernel/fs/file_world.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/fs/file_table.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/vmm.hpp"
#include "kernel/arch/armv7/isr.hpp"
#include "kernel/mm/demand.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/proc/fault_resolve.hpp"
#include "kernel/proc/scheduler.hpp"

/// The vector stub's seated door: raised while seated episodes run, so
/// their IRQ frames take the task-owned homing path.
extern "C" unsigned long c_seat_irq_door;
#include "kernel/proc/task.hpp"

/// The trap frame the seat is servicing; born in user_mode.cpp with
/// the bring-up world, now the seat's own word.
extern "C" cinux::arch::UserTrap* g_current_trap = nullptr;

extern "C" char SeatProgStart[];
extern "C" char SeatProgEnd[];

namespace cinux::armv7 {

cinux::base::Shared<cinux::fs::FileTable> seated_files() {
    proc::Task* const kTask = proc::Scheduler::self().current();
    return kTask == nullptr || kTask->arch_seat == nullptr
               ? cinux::base::Shared<cinux::fs::FileTable>{}
               : kTask->files;
}

bool SeatWorldActive() {
    return current_seat() != nullptr;
}

void SeatReturnToKernel() {
    // Runs in the service-call context on the seated task's own stack.
    // The switch parks this kernel half into a scratch image and resumes
    // the body right after its EnterUserModeWithReturn; the scratch dies
    // with the episode, so nothing here needs the trap frame to double
    // as a context image.
    SeatRun* const     kSeat = current_seat();
    arch::CpuContext parked{};
    arch::ContextSwitch(&parked, kSeat->body);
    __builtin_unreachable();
}

/// A fault the seat could not repair: the same door a voluntary exit
/// takes, with the fault word the reaper expects.
void SeatFaultDeath() {
    SeatExit(0xDEADUL);
}

void SeatExit(unsigned long status) {
    // The exit service: same switch as the return, but the body reads
    // the exit word and retires through the scheduler's exit path.
    current_seat()->exited      = true;
    current_seat()->exit_status = status;
    SeatReturnToKernel();
}

extern "C" void SaveContextResumeTrap(cinux::arch::CpuContext* save,
                                      cinux::arch::UserTrap* trap);

/// The fork child's first resume: the neutral fork path arms the young
/// task's context here with the parent's episode frame copied to the
/// child's kernel stack top. This entry is the child's own seat body:
/// it adopts the seat state, leaves for user mode with r0 zero, and a
/// later park or exit switches back into this frame to retire.
extern "C" void SeatForkChildEntry() {
    proc::Task* const kSelf = proc::Scheduler::self().current();
    // The frame address is a fact of the task's stack top, not of this
    // call: the compiler's own prologue has already spent words by the
    // time any local runs.
    auto* const kTrap = reinterpret_cast<cinux::arch::UserTrap*>(
        kSelf->stack_base + proc::kStackBytes - sizeof(cinux::arch::UserTrap));
    cinux::arch::CpuContext body{};
    SeatRun run{.space  = kSelf->space.get(),
                .ranges = kSelf->vmas.get(),
                .body   = &body};
    kSelf->arch_seat = &run;

    cinux::arch::space::SwitchRoot(kSelf->user_root);
    SaveContextResumeTrap(&body, kTrap);  // leaves for user mode, r0 zero
    // A park or exit switched back here: the run is over either way.
    cinux::arch::space::SwitchRoot(mm::KernelPageRoot());
    run.spoke       = true;
    kSelf->arch_seat = nullptr;
    proc::Scheduler::self().exit_current(static_cast<long long>(run.exit_status));
}

}  // namespace cinux::armv7
