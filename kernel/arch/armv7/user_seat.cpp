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
 * Two worlds ride this seat today: an inline probe program and the
 * loader's ELF guest; both are ordinary processes from here on.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.2.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/user_seat.hpp"
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
#include "kernel/proc/scheduler.hpp"

/// The vector stub's seated door: raised while seated episodes run, so
/// their IRQ frames take the task-owned homing path.
extern "C" unsigned long c_seat_irq_door;
#include "kernel/proc/task.hpp"

/// The trap frame the seat is servicing; born in user_mode.cpp with
/// the bring-up world, now the seat's own word.
extern "C" cinux::arch::UserTrap* g_current_trap = nullptr;

using FaultRepair = bool (*)(unsigned long address, unsigned long status);
void InstallFaultRepair(FaultRepair hook);

extern "C" char SeatProgStart[];
extern "C" char SeatProgEnd[];

namespace cinux::armv7 {

namespace {

/// The launch in flight: the one world being seated right now. The
/// body copies it into the task's own seat state and the launcher
/// reads the run's verdict back from that state.
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

struct SeatLaunch {
    mm::AddressSpace* space     = nullptr;
    mm::VmaStore*     ranges    = nullptr;
    SeatRun*          run       = nullptr;
    unsigned long     entry     = 0;
    unsigned long     stack_top = 0;
};

SeatLaunch g_seat_launch{};

/// The current process's seat state, or null while none runs.
SeatRun* current_seat() {
    proc::Task* const kTask = proc::Scheduler::self().current();
    return kTask == nullptr ? nullptr : static_cast<SeatRun*>(kTask->arch_seat);
}

/// One shared body for every seated process: enter the user world,
/// come back when it asks, then retire with its exit word.
void SeatProcessBody() {
    SeatRun* const kRun = g_seat_launch.run;
    cinux::arch::CpuContext body{};
    kRun->body            = &body;
    mm::AddressSpace* const kSpace = kRun->space;

    // Seat this task's own files: the console stream answers through
    // the same /dev/console every lane's seated process reads.
    const cinux::base::Shared<cinux::fs::FileTable> kFiles =
        proc::Scheduler::self().current()->files;
    static_cast<void>(fs::SeatConsoleStreams(userfs::vfs(), *kFiles));

    const unsigned long kKernelRoot = mm::KernelPageRoot();
    cinux::arch::per_cpu::self().slice_ticks = 1000;  // the seat window owns the slice
    cinux::arch::space::SwitchRoot(kSpace->root());
    arch::EnterUserModeWithReturn(g_seat_launch.entry, g_seat_launch.stack_top, &body);
    // A service call landed here with the user world parked.
    cinux::arch::space::SwitchRoot(kKernelRoot);
    kRun->spoke = true;

    // The run's words die with this frame: retire the seat marker
    // before the scheduler can hand the task back, or a stale current
    // task makes the old-world episodes look seated.
    proc::Scheduler::self().current()->arch_seat = nullptr;
    proc::Scheduler::self().exit_current(static_cast<long long>(kRun->exit_status));
}

/// Runs one seated world through a fresh scheduler task.
bool run_seated_world(mm::AddressSpace* space, unsigned long entry, unsigned long stack_top,
                      bool /*owns_space*/, bool expect_exit, unsigned long expect_status,
                      const char* name) {
    mm::VmaStore* const kCarried = g_seat_launch.ranges;  // a caller's store rides along
    SeatRun run{.ranges = kCarried};
    g_seat_launch = SeatLaunch{.space     = space,
                               .ranges    = kCarried,
                               .run       = &run,
                               .entry     = entry,
                               .stack_top = stack_top};
    proc::Task* const kTask =
        proc::TaskBuilder().set_entry(SeatProcessBody).set_name(name).build();
    if (kTask == nullptr) {
        return false;
    }
    // The register of promises the neutral demand pager fills against;
    // the loader already wrote the guest's segments into its own store,
    // the probe starts a fresh one.
    if (run.ranges == nullptr) {
        kTask->vmas = base::MakeShared<mm::VmaStore>();
        run.ranges  = kTask->vmas.get();
        g_seat_launch.ranges = run.ranges;
    } else {
        kTask->vmas = base::Shared<mm::VmaStore>(run.ranges);
    }
    // The task owns the space: the neutral syscall faces read it, and
    // its Shared release runs the teardown once the task retires.
    kTask->space    = base::Shared<mm::AddressSpace>(space);
    kTask->user_root = space->root();
    // The running state rides the task: every episode side reaches it
    // through arch_seat, and the launcher reads the verdict here.
    run.space = space;
    kTask->arch_seat = &run;
    if (!proc::Scheduler::self().seat(*kTask)) {
        return false;
    }
    proc::Scheduler::self().run_until_done();
    if (!run.spoke) {
        return false;
    }
    return !expect_exit || (run.exited && run.exit_status == expect_status);
}

/// Runs one seated world that carries its own promise register: the
/// guest's segments were written into the loader's store.
bool run_seated_world_with_ranges(mm::AddressSpace* space, unsigned long entry,
                                  unsigned long stack_top, bool owns_space, bool expect_exit,
                                  unsigned long expect_status, const char* name,
                                  mm::VmaStore* ranges) {
    g_seat_launch.ranges = ranges;
    // The seated episodes may park mid-frame while the neutral scheduler
    // lends the CPU elsewhere, so their IRQ frames need the task-owned
    // door and their ticks need the scheduler's preemption.
    ::c_seat_irq_door = 1;
    cinux::proc::Scheduler::self().set_preemption(true);
    const bool kOk = run_seated_world(space, entry, stack_top, owns_space, expect_exit,
                                      expect_status, name);
    cinux::proc::Scheduler::self().set_preemption(false);
    ::c_seat_irq_door = 0;
    g_seat_launch.ranges = nullptr;
    return kOk;
}

/// Builds the probe's own world: one code page, one stack page.
bool run_probe_world() {
    auto* const kSpace = new mm::AddressSpace{};
    if (kSpace == nullptr || !kSpace->init()) {
        return false;
    }
    const auto kCode  = mm::Pmm::self().allocate_page();
    const auto kStack = mm::Pmm::self().allocate_page();
    if (kCode.raw == 0 || kStack.raw == 0) {
        delete kSpace;
        return false;
    }
    const unsigned long kProgBytes = static_cast<unsigned long>(SeatProgEnd - SeatProgStart);
    auto* const kCodeBytes = reinterpret_cast<unsigned char*>(cinux::mm::DirectMapVirt(kCode.raw));
    for (unsigned long index = 0; index < cinux::arch::page::kSize; ++index) {
        kCodeBytes[index] =
            index < kProgBytes ? static_cast<unsigned char>(SeatProgStart[index]) : 0U;
    }
    // The code page is law: read-only and executable. A guest write
    // against it faults, and the seat's repair seam hands back a
    // private writable copy through the one armv7 cow door.
    if (!kSpace->map_user(0x40000000UL, kCode.raw,
                          cinux::arch::page::UserLeafTerms(false, true)) ||
        !kSpace->map_user(0x40010000UL, kStack.raw)) {
        kSpace->teardown();
        delete kSpace;
        return false;
    }
    return run_seated_world(kSpace, 0x40000000UL, 0x40011000UL, true, false, 0, "seatprog");
}

/// The abort repair door: the seated world's not-present pages are the
/// neutral demand pager's business, and its write faults against shared
/// or read-only leaves go through the one armv7 copy seam - the same
/// RepairWriteFault the legacy task world serves. One fill or one
/// private copy, and the instruction that faulted runs again.
bool SeatRepairPage(unsigned long address, unsigned long status) {
    // The seat's cached worlds go stale the moment an exec replaces
    // them, so the live task's own holds are the only truth here: read
    // them fresh on every fault instead of trusting the launch-time
    // pointers the run was born with.
    proc::Task* const kTask = proc::Scheduler::self().current();
    if (current_seat() == nullptr || kTask == nullptr || !kTask->space.holds() ||
        !kTask->vmas.holds()) {
        return false;
    }
    if ((status & arch::isr::kDfsrWrite) != 0 &&
        mm::cow::RepairWriteFault(kTask->space->root(), address)) {
        return true;
    }
    unsigned long spsr = 0;
    asm volatile("mrs %0, spsr" : "=r"(spsr));
    const unsigned long long kCode = arch::isr::FaultCodeFromStatus(status, spsr);
    const mm::demand::FillOutcome kFilled =
        mm::demand::FillPage(*kTask->space, *kTask->vmas, address, kCode);
    return kFilled != mm::demand::FillOutcome::kUnhandled;
}

}  // namespace

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
    // The user's saved frame is this episode's own stack frame; its
    // r4..spsr words are a CpuContext image, and the switch returns
    // into the body right after its EnterUserModeWithReturn. The body
    // resumes with its pre-user sp, so the frame's 64 bytes die with
    // the episode.
    SeatRun* const kSeat = current_seat();
    arch::CpuContext* const kUserWords =
        reinterpret_cast<arch::CpuContext*>(&g_current_trap->r4);
    arch::ContextSwitch(kUserWords, kSeat->body);
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

bool RunSchedulerUserSelfCheck() {
    // The seated world owns the user file system now: ram files, the
    // devfs tree and the guest ELF all come up with it.
    userfs::init();
    InstallFaultRepair(&SeatRepairPage);
    if (!run_probe_world()) {
        return false;
    }
    // The loader's guest is no ordinary probe: it is the first cross-
    // compiled process, and from here on it rides the real scheduler
    // like any process would.
    mm::AddressSpace* const kGuest = userfs::take_guest();
    if (kGuest == nullptr) {
        return false;
    }
    cinux::print::Println("[armv7] guest runs on the scheduler entry %X",
                          userfs::guest_entry());
    mm::VmaStore* const kGuestRanges = userfs::take_guest_ranges();
    const bool kGuestOk = run_seated_world_with_ranges(
        kGuest, userfs::guest_entry(), 0x40011000UL, true, true, 0, "guest", kGuestRanges);
    cinux::print::Println(kGuestOk ? "[armv7] guest exited with status 0"
                                   : "[armv7] guest run FAILED");
    return kGuestOk;
}

}  // namespace cinux::armv7
