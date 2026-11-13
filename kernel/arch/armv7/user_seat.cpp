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
#include "kernel/proc/task.hpp"

extern "C" cinux::arch::UserTrap g_user_trap;

using FaultRepair = bool (*)(unsigned long address, unsigned long status);
void InstallFaultRepair(FaultRepair hook);

extern "C" char SeatProgStart[];
extern "C" char SeatProgEnd[];

namespace cinux::armv7 {

namespace {

/// The seated process's open files; the console faces read it before
/// the bring-up round-robin's table. Null while no seated process runs.
cinux::base::Shared<cinux::fs::FileTable> g_seat_files{};

/// Where the waiting kernel body's saved machine state lives; the
/// return and exit services switch back to it.
cinux::arch::CpuContext* g_seat_body = nullptr;

/// The world the running body entered: the probe builds and owns its
/// pages, the guest hands over the loader's already-built world.
struct SeatWorld {
    mm::AddressSpace* space       = nullptr;
    unsigned long     entry       = 0;
    unsigned long     stack_top   = 0;
    unsigned long     exit_status = 0;
    bool              exited      = false;
    bool              spoke       = false;
    mm::VmaStore*     ranges      = nullptr;
};

SeatWorld g_seat_world{};

/// One shared body for every seated process: enter the user world,
/// come back when it asks, then retire with its exit word.
void SeatProcessBody() {
    cinux::arch::CpuContext body{};
    g_seat_body          = &body;
    mm::AddressSpace* const kSpace = g_seat_world.space;

    // Seat this task's own files: the console stream answers through
    // the same /dev/console every lane's seated process reads.
    g_seat_files = proc::Scheduler::self().current()->files;
    static_cast<void>(fs::SeatConsoleStreams(userfs::vfs(), *g_seat_files));

    const unsigned long kKernelRoot = mm::KernelPageRoot();
    cinux::arch::per_cpu::self().slice_ticks = 1000;  // the seat window owns the slice
    cinux::arch::space::SwitchRoot(kSpace->root());
    arch::EnterUserModeWithReturn(g_seat_world.entry, g_seat_world.stack_top, &body);
    // A service call landed here with the user world parked.
    cinux::arch::space::SwitchRoot(kKernelRoot);
    g_seat_files.reset();
    g_seat_world.spoke = true;

    proc::Scheduler::self().exit_current(static_cast<long long>(g_seat_world.exit_status));
}

/// Runs one seated world through a fresh scheduler task.
bool run_seated_world(mm::AddressSpace* space, unsigned long entry, unsigned long stack_top,
                      bool /*owns_space*/, bool expect_exit, unsigned long expect_status,
                      const char* name) {
    mm::VmaStore* const kCarried = g_seat_world.ranges;  // a caller's store rides along
    g_seat_world = SeatWorld{.space     = space,
                             .entry     = entry,
                             .stack_top = stack_top,
                             .ranges    = kCarried};
    proc::Task* const kTask =
        proc::TaskBuilder().set_entry(SeatProcessBody).set_name(name).build();
    if (kTask == nullptr) {
        return false;
    }
    // The register of promises the neutral demand pager fills against;
    // the loader already wrote the guest's segments into its own store,
    // the probe starts a fresh one.
    if (g_seat_world.ranges == nullptr) {
        kTask->vmas = base::MakeShared<mm::VmaStore>();
        g_seat_world.ranges = kTask->vmas.get();
    } else {
        kTask->vmas = base::Shared<mm::VmaStore>(g_seat_world.ranges);
    }
    // The task owns the space: the neutral syscall faces read it, and
    // its Shared release runs the teardown once the task retires.
    kTask->space    = base::Shared<mm::AddressSpace>(space);
    kTask->user_root = space->root();
    if (!proc::Scheduler::self().seat(*kTask)) {
        return false;
    }
    proc::Scheduler::self().run_until_done();
    if (!g_seat_world.spoke) {
        return false;
    }
    return !expect_exit ||
           (g_seat_world.exited && g_seat_world.exit_status == expect_status);
}

/// Runs one seated world that carries its own promise register: the
/// guest's segments were written into the loader's store.
bool run_seated_world_with_ranges(mm::AddressSpace* space, unsigned long entry,
                                  unsigned long stack_top, bool owns_space, bool expect_exit,
                                  unsigned long expect_status, const char* name,
                                  mm::VmaStore* ranges) {
    g_seat_world.ranges = ranges;
    const bool kOk = run_seated_world(space, entry, stack_top, owns_space, expect_exit,
                                      expect_status, name);
    g_seat_world.ranges = nullptr;
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
    if (!SeatWorldActive() || g_seat_world.ranges == nullptr) {
        return false;
    }
    if ((status & arch::isr::kDfsrWrite) != 0 &&
        mm::cow::RepairWriteFault(g_seat_world.space->root(), address)) {
        return true;
    }
    unsigned long spsr = 0;
    asm volatile("mrs %0, spsr" : "=r"(spsr));
    const unsigned long long kCode = arch::isr::FaultCodeFromStatus(status, spsr);
    const mm::demand::FillOutcome kFilled =
        mm::demand::FillPage(*g_seat_world.space, *g_seat_world.ranges, address, kCode);
    return kFilled != mm::demand::FillOutcome::kUnhandled;
}

}  // namespace

cinux::base::Shared<cinux::fs::FileTable> seated_files() {
    return g_seat_files;
}

bool SeatWorldActive() {
    return g_seat_files.holds();
}

void SeatReturnToKernel() {
    // Runs in the service-call context on the seated task's own stack.
    // The user's saved frame lives in g_user_trap; its r4..spsr words
    // are a CpuContext image, and the switch returns into the body
    // right after its EnterUserModeWithReturn.
    g_seat_files.reset();
    arch::CpuContext* const kUserWords = reinterpret_cast<arch::CpuContext*>(&g_user_trap.r4);
    arch::ContextSwitch(kUserWords, g_seat_body);
    __builtin_unreachable();
}

void SeatExit(unsigned long status) {
    // The exit service: same switch as the return, but the body reads
    // the exit word and retires through the scheduler's exit path.
    g_seat_world.exited      = true;
    g_seat_world.exit_status = status;
    SeatReturnToKernel();
}

bool RunSchedulerUserSelfCheck() {
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
