/**
 * @file    user_seat.cpp
 * @brief   A user process seated on the real scheduler: the armv7 step
 *          toward the x86 model where a Task carries its user world.
 *
 * The bring-up mini round-robin keeps its user worlds on g_tasks; the
 * shared scheduler runs kernel tasks today. This probe joins the two
 * halves: a real proc::Task whose body drops to user mode on its own
 * stack, issues one console write through the seated file table, and
 * returns through a dedicated service call — the same exception-return
 * trick ContextSwitch already speaks. The seat's file table is the
 * bridge the console faces read, so the write crosses the very same
 * neutral FileTable and console ops the loaded guest uses.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/user_seat.hpp"

#include "kernel/arch/space_control.hpp"
#include "kernel/arch/armv7/per_cpu.hpp"
#include "kernel/mm/pmm.hpp"

#include "kernel/arch/armv7/context.hpp"
#include "kernel/arch/armv7/selfcheck.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/space_layout.hpp"
#include "kernel/arch/armv7/user_entry.hpp"
#include "kernel/arch/armv7/user_fs.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/fs/file_table.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/vmm.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/task.hpp"

extern "C" cinux::arch::UserTrap g_user_trap;

extern "C" char SeatProgStart[];
extern "C" char SeatProgEnd[];

namespace cinux::armv7 {

namespace {

/// The seated process's open files; the console faces read it before
/// the mini round-robin's own table. Null while no seated process runs.
cinux::base::Shared<cinux::fs::FileTable> g_seat_files{};

/// Where the waiting kernel body's saved machine state lives; the
/// return service call switches back to it.
cinux::arch::CpuContext* g_seat_body = nullptr;

bool g_seat_wrote = false;

/// The task's body: run on the task's own stack, drop to user mode,
/// and come back when the program asks the kernel to take it.
void SeatProcessBody() {
    cinux::arch::CpuContext body{};
    g_seat_body = &body;

    auto* const kSpace = new mm::AddressSpace{};
    if (kSpace == nullptr || !kSpace->init()) {
        proc::Scheduler::self().exit_current(1);
    }
    const auto kCode = mm::Pmm::self().allocate_page();
    const auto kStack = mm::Pmm::self().allocate_page();
    if (kCode.raw == 0 || kStack.raw == 0) {
        kSpace->teardown();
        delete kSpace;
        proc::Scheduler::self().exit_current(1);
    }
    const unsigned long kProgBytes =
        static_cast<unsigned long>(SeatProgEnd - SeatProgStart);
    auto* const kCodeBytes = reinterpret_cast<unsigned char*>(cinux::mm::DirectMapVirt(kCode.raw));
    for (unsigned long index = 0; index < cinux::arch::page::kSize; ++index) {
        kCodeBytes[index] = index < kProgBytes ? static_cast<unsigned char>(SeatProgStart[index]) : 0U;
    }
    if (!kSpace->map_user(0x40000000UL, kCode.raw) ||
        !kSpace->map_user(0x40010000UL, kStack.raw)) {
        kSpace->teardown();
        delete kSpace;
        proc::Scheduler::self().exit_current(1);
    }

    // Seat this task's own files: the console faces answer through them.
    g_seat_files = proc::Scheduler::self().current()->files;
    const fs::FsOps* const kFace  = userfs::console_ops();
    const fs::FsNode        kNode{nullptr, fs::InodeType::kCharDevice, 0, 0};
    const auto kOut = base::MakeShared<fs::OpenFile>(kFace, nullptr, kNode);
    kOut->status_flags = 2;  // writable
    static_cast<void>(g_seat_files->adopt_at(kOut, 1));

    const unsigned long kKernelRoot = mm::KernelPageRoot();
    cinux::arch::per_cpu::self().slice_ticks = 1000;  // the probe window owns the slice
    cinux::arch::space::SwitchRoot(kSpace->root());
    arch::EnterUserModeWithReturn(0x40000000UL, 0x40011000UL, &body);
    cinux::arch::space::SwitchRoot(kKernelRoot);
    g_seat_wrote = true;
    g_seat_files.reset();

    kSpace->teardown();
    delete kSpace;
    proc::Scheduler::self().exit_current(0);
}

}  // namespace

cinux::base::Shared<cinux::fs::FileTable> seated_files() {
    return g_seat_files;
}

void SeatReturnToKernel() {
    // Runs in the service-call context on the seated task's own stack.
    // The user's saved frame lives in g_user_trap; its r4..spsr words
    // are a CpuContext image, and the switch returns into the body
    // right after its EnterUserModeWithReturn.
    g_seat_files.reset();
    arch::CpuContext* const kUserWords =
        reinterpret_cast<arch::CpuContext*>(&g_user_trap.r4);
    arch::ContextSwitch(kUserWords, g_seat_body);
    __builtin_unreachable();
}

bool RunSchedulerUserSelfCheck() {
    g_seat_wrote = false;
    proc::Task* const kTask =
        proc::TaskBuilder().set_entry(SeatProcessBody).set_name("seatprog").build();
    if (kTask == nullptr) {
        return false;
    }
    if (!proc::Scheduler::self().seat(*kTask)) {
        return false;
    }
    proc::Scheduler::self().run_until_done();
    return g_seat_wrote;
}

}  // namespace cinux::armv7
