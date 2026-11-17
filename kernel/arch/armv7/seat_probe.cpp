/**
 * @file    seat_probe.cpp
 * @brief   The self-check image's seated probe and the launcher for it.
 *
 * This world exists only in arm_selftest, the image the arm_boot lane
 * boots. The production kernel keeps the seat machinery in user_seat.cpp
 * and never carries this program or its bytes.
 *
 * A seated process is a real proc::Task whose body drops to user mode
 * on its own stack. The probe proves the scheduler can seat a user task,
 * let it speak through the console face, and retire it through the
 * normal exit service.
 *
 * @author  Charliechen114514
 * @date    2026-11-17
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/seat_run.hpp"

#include "kernel/arch/armv7/selfcheck.hpp"
#include "kernel/arch/armv7/user_seat.hpp"

#include "kernel/arch/space_control.hpp"
#include "kernel/arch/armv7/per_cpu.hpp"
#include "kernel/mm/pmm.hpp"

#include "kernel/arch/armv7/context.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/user_entry.hpp"
#include "kernel/arch/armv7/user_fs.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/fs/file_table.hpp"
#include "kernel/fs/file_world.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/mm/vmm.hpp"
#include "kernel/proc/fault_resolve.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/task.hpp"

/// The vector stub's seated door: raised while seated episodes run, so
/// their IRQ frames take the task-owned homing path.
extern "C" unsigned long c_seat_irq_door;

extern "C" char SeatProgStart[];
extern "C" char SeatProgEnd[];

namespace cinux::armv7 {
namespace {

/// The launch in flight: the one world being seated right now. The body
/// copies it into the task's own seat state and the launcher reads the
/// run's verdict back from that state.
struct SeatLaunch {
    mm::AddressSpace* space     = nullptr;
    mm::VmaStore*     ranges    = nullptr;
    SeatRun*          run       = nullptr;
    unsigned long     entry     = 0;
    unsigned long     stack_top = 0;
};

SeatLaunch g_seat_launch{};

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
    // The register of promises the neutral demand pager fills against:
    // the caller hands one over, and a fresh store serves otherwise.
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
/// caller already wrote the program's segments into it.
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
    auto* const kCodeBytes =
        reinterpret_cast<unsigned char*>(cinux::mm::DirectMapVirt(kCode.raw));
    for (unsigned long index = 0; index < cinux::arch::page::kSize; ++index) {
        kCodeBytes[index] =
            index < kProgBytes ? static_cast<unsigned char>(SeatProgStart[index]) : 0U;
    }
    // The code page is law: read-only and executable. The range below
    // promises no write either, so a write against it dies at the
    // resolver instead of earning a private copy.
    if (!kSpace->map_user(0x40000000UL, kCode.raw,
                          cinux::arch::page::UserLeafTerms(false, true)) ||
        !kSpace->map_user(0x40010000UL, kStack.raw)) {
        kSpace->teardown();
        delete kSpace;
        return false;
    }
    // The probe gets a real promise register like any process: the
    // neutral resolver reads the faulting address against it, so a
    // page mapped with no region behind it is a fault nobody can
    // repair — which is exactly what the seat used to paper over with
    // a private repair hook.
    auto* const kRanges = new mm::VmaStore{};
    if (kRanges == nullptr) {
        kSpace->teardown();
        delete kSpace;
        return false;
    }
    mm::VmaRegion code_range{};
    code_range.start      = 0x40000000UL;
    code_range.end        = 0x40001000UL;
    code_range.kind       = mm::VmaKind::kAnonymous;
    code_range.readable   = true;
    code_range.executable = true;
    mm::VmaRegion stack_range{};
    stack_range.start       = 0x40010000UL;
    stack_range.end         = 0x40011000UL;
    stack_range.kind        = mm::VmaKind::kStack;
    stack_range.readable    = true;
    stack_range.writable    = true;
    stack_range.stack_floor = 0x40010000UL;
    if (!kRanges->register_range(code_range).ok() ||
        !kRanges->register_range(stack_range).ok()) {
        delete kRanges;
        kSpace->teardown();
        delete kSpace;
        return false;
    }
    return run_seated_world_with_ranges(kSpace, 0x40000000UL, 0x40011000UL, true, false, 0,
                                         "seatprog", kRanges);
}

}  // namespace

bool RunSchedulerUserSelfCheck() {
    // The seated world owns the user file system now: the card's volume
    // and the devfs tree come up with it.
    userfs::init();
    // The neutral resolver: CoW first, then the register's promises. It
    // is the same door the x86 lane seats, and the death a fatal fault
    // means stays here — SeatFaultDeath retires the episode, which is
    // what this world's protocol expects.
    proc::InstallFaultResolver();
    return run_probe_world();
}

}  // namespace cinux::armv7
