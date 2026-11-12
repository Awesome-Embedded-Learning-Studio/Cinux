/**
 * @file    user_mode.cpp
 * @brief   Two user tasks, each with its own table, yielding to each other.
 *
 * The svc stub traps a user task by saving its frame (r4-r11, user sp,
 * user pc, SPSR) into g_user_save_area. c_schedule_from_user copies that
 * frame into the running task, picks the next runnable one, points TTBR0
 * at its table, and hands its frame back; the stub restores it through an
 * exception return. Exiting marks the task done and either starts the
 * other or resumes the kernel.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.6
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include <cstdint>

#include "kernel/arch/armv7/context.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/arch/armv7/syscall_numbers.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/page_walk.hpp"
#include "kernel/arch/space.hpp"
#include "kernel/arch/isr.hpp"
#include "kernel/arch/armv7/cow_ledger.hpp"
#include "kernel/arch/armv7/user_access.hpp"
#include "kernel/arch/armv7/per_cpu.hpp"
#include "kernel/arch/armv7/user_entry.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/page_walk.hpp"
#include "kernel/arch/space_control.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/arch/armv7/cow_ledger.hpp"
#include "kernel/arch/armv7/user_access.hpp"
#include "kernel/arch/armv7/clone_space.hpp"
#include "kernel/arch/armv7/shared_map.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vmm.hpp"

using FaultRepair = bool (*)(unsigned long address, unsigned long status);
void InstallFaultRepair(FaultRepair hook);

extern "C" unsigned char __user_text_start[];
extern "C" unsigned char __user_text_end[];

namespace {

constexpr unsigned long kTaskACode  = 0x40001000UL;
constexpr unsigned long kTaskAStack = 0x40002000UL;
constexpr unsigned long kTaskBCode  = 0x40003000UL;
constexpr unsigned long kTaskBStack = 0x40004000UL;

enum class TaskState : unsigned char { kRunnable, kBlocked, kDone };

struct UserTask {
    cinux::arch::UserTrap    trap;
    cinux::mm::AddressSpace* space;
    TaskState                state;
    unsigned long            exit_status;
    unsigned long            wait_target;   // valid while blocked
    unsigned long            wait_mask;     // bit per awaited task
    unsigned long            wait_deadline; // ticks; valid while blocked
};

constexpr unsigned long kTaskCount   = 4;
constexpr unsigned long kWaitTicks  = 20;  // default deadline when the caller passes 0

UserTask                g_tasks[kTaskCount];
cinux::arch::CpuContext g_main;
cinux::arch::CpuContext g_scratch;
unsigned long           g_kernel_root = 0;
unsigned long           g_current     = 0;
bool                    g_running     = false;
unsigned long long      g_shared_frame = 0;
unsigned long           g_tick_now     = 0;  // refreshed by the IRQ path

long user_syscall0(unsigned long number) {
    register unsigned long r7 asm("r7") = number;
    register unsigned long r0 asm("r0") = 0;
    register unsigned long r1 asm("r1") = 0;
    register unsigned long r2 asm("r2") = 0;
    register unsigned long r3 asm("r3") = 0;
    // A trapping svc keeps no promise about r0-r3; tell the compiler.
    asm volatile("svc #0"
                 : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                 : "r"(r7)
                 : "cc", "memory", "ip", "lr");
    return static_cast<long>(r0);
}

long user_syscall3(unsigned long number, unsigned long arg0, unsigned long arg1,
                   unsigned long arg2) {
    register unsigned long r7 asm("r7") = number;
    register unsigned long r0 asm("r0") = arg0;
    register unsigned long r1 asm("r1") = arg1;
    register unsigned long r2 asm("r2") = arg2;
    register unsigned long r3 asm("r3") = 0;
    asm volatile("svc #0"
                 : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
                 : "r"(r7)
                 : "cc", "memory", "ip", "lr");
    return static_cast<long>(r0);
}

void user_write(const char* text, unsigned long length) {
    static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kWrite, 1,
                                    static_cast<unsigned long>(
                                        reinterpret_cast<std::uintptr_t>(text)),
                                    length));
}

extern "C" void user_entry_stub();

/// Runs on a user page; reaches the kernel only through svc.
extern "C" [[noreturn]] __attribute__((section(".user_text"))) void user_program_c(
    unsigned long task_id) {
    char          buffer[64];
    unsigned long length = 0;
#define PUSH_CHAR(c) buffer[length++] = (c)
    PUSH_CHAR('0' + static_cast<char>(task_id));
    PUSH_CHAR(':');
    PUSH_CHAR('?');
    PUSH_CHAR('['); PUSH_CHAR('a'); PUSH_CHAR('r'); PUSH_CHAR('m'); PUSH_CHAR('v');
    PUSH_CHAR('7'); PUSH_CHAR(']'); PUSH_CHAR(' '); PUSH_CHAR('u'); PUSH_CHAR('s');
    PUSH_CHAR('e'); PUSH_CHAR('r'); PUSH_CHAR(' '); PUSH_CHAR('w'); PUSH_CHAR('r');
    PUSH_CHAR('i'); PUSH_CHAR('t'); PUSH_CHAR('e'); PUSH_CHAR('\n');
#undef PUSH_CHAR
    // Write this task's own value into the shared page; the first write
    // takes a copy-on-write fault. It is read back only after the loop,
    // by which time preemption has let the other task write its own value
    // into the same address.
    constexpr unsigned long kSharedVa = 0x40008000UL;
    const unsigned long     kValue    = 0x1000UL + task_id;
    *reinterpret_cast<volatile unsigned long*>(kSharedVa) = kValue;

    // A pointer into the kernel's own memory must be refused, not read.
    static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kWrite, 1, 0x80000000UL, 8));
    // Task 0 finishes early so its wait really has to block.
    const unsigned long kRounds = task_id == 0 ? 2UL : 20UL;
    for (unsigned long round = 0; round < kRounds; ++round) {
        buffer[2] = static_cast<char>('0' + (round % 10));
        user_write(buffer, length);
        for (volatile unsigned long spin = 0; spin < 6000000UL; ++spin) {
        }

    }
    {
        char          note[32];
        unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
        NOTE('t'); NOTE('a'); NOTE('s'); NOTE('k'); NOTE(' ');
        NOTE(static_cast<char>('0' + task_id));
        NOTE(' ');
        if (*reinterpret_cast<volatile unsigned long*>(kSharedVa) == kValue) {
            NOTE('k'); NOTE('e'); NOTE('e'); NOTE('p'); NOTE('s'); NOTE(' ');
            NOTE('i'); NOTE('t'); NOTE('s'); NOTE(' '); NOTE('p'); NOTE('a');
            NOTE('g'); NOTE('e'); NOTE('\n');
        } else {
            NOTE('l'); NOTE('o'); NOTE('s'); NOTE('t'); NOTE(' '); NOTE('i');
            NOTE('t'); NOTE('s'); NOTE(' '); NOTE('p'); NOTE('a'); NOTE('g');
            NOTE('e'); NOTE('\n');
        }
#undef NOTE
        user_write(note, note_length);
    }
    {
        const long kPid = user_syscall3(cinux::armv7::syscall_nr::kGetPid, 0, 0, 0);
        if (static_cast<unsigned long>(kPid) == task_id) {
            char          note[16];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('p'); NOTE('i'); NOTE('d'); NOTE(' ');
            NOTE(static_cast<char>('0' + task_id)); NOTE(' '); NOTE('o'); NOTE('k');
            NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    if (task_id == 0) {
        const long kSlept = user_syscall3(cinux::armv7::syscall_nr::kSleep, 3, 0, 0);
        if (kSlept == 0) {
            char          note[16];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('s'); NOTE('l'); NOTE('e'); NOTE('p'); NOTE('t'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    if (task_id == 0) {
        // Waiting on itself can never be satisfied: it must time out.
        unsigned long status = 0;
        const long    kSelf  = user_syscall3(
            cinux::armv7::syscall_nr::kWait, 1UL,
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&status)), 5);
        if (kSelf == -2) {
            char          note[32];
            unsigned long note_length = 0;
#define NOTE(c) note[note_length++] = (c)
            NOTE('s'); NOTE('e'); NOTE('l'); NOTE('f'); NOTE(' ');
            NOTE('w'); NOTE('a'); NOTE('i'); NOTE('t'); NOTE(' ');
            NOTE('t'); NOTE('i'); NOTE('m'); NOTE('e'); NOTE('d'); NOTE(' ');
            NOTE('o'); NOTE('u'); NOTE('t'); NOTE('\n');
#undef NOTE
            user_write(note, note_length);
        }
    }
    if (task_id == 0 || task_id == 3) {
        // Task 0 reaps 1 then 2; task 3 only reaps 2, so a wake aimed at 1
        // must leave it asleep.
        unsigned long status = 0;
        const unsigned long kTargets[] = {1UL, 2UL};
        const unsigned long kCount     = task_id == 0 ? 2UL : 1UL;
        for (unsigned long index = 0; index < kCount; ++index) {
            const unsigned long kTarget =
                task_id == 0 ? kTargets[index] : kTargets[1];
            for (unsigned long attempt = 0; attempt < 400000UL; ++attempt) {
                const long kResult = user_syscall3(
                    cinux::armv7::syscall_nr::kWait, 1UL << kTarget,
                    static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&status)), 0);
                if (kResult >= 0) {
                    break;
                }
            }
        }
    }
    {
        // Inside the user window but never mapped: the repair hook fills it
        // and the store is retried on the new page.
        auto* const kProbe = reinterpret_cast<volatile unsigned long*>(0x40009000UL);
        *kProbe            = 0xC0FFEEUL;
    }
    {
        char       line[16];
        const long kReceived = user_syscall3(
            cinux::armv7::syscall_nr::kRead, 0,
            static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(line)), sizeof(line));
        static_cast<void>(kReceived);
    }
    if (task_id != 0) {
        // Not mapped in the clone either: this must still end the task.
        static_cast<void>(*reinterpret_cast<volatile unsigned long*>(0x4000B000UL));
    }
    static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kExit, 7, 0, 0));
    for (;;) {
        asm volatile("wfi");
    }
}

bool map_user_page(unsigned long root, unsigned long virtual_address, unsigned long physical) {
    cinux::mm::KernelTables tables;
    cinux::arch::page::Entry* table =
        cinux::mm::walk::EnsureLeafTable(tables, root, virtual_address,
                                         cinux::arch::page::kUser);
    if (table == nullptr) {
        return false;
    }
    table[cinux::arch::page::SlotIndex(virtual_address, cinux::arch::page::kLeafLevel)] =
        cinux::arch::page::MakePageEntry(physical, cinux::arch::page::kUser |
                                                       cinux::arch::page::kWritable);
    return true;
}

/// Gives a cloned task a private stack page and its own argument, so the
/// clone shares code and data but not its stack.
bool privatize_stack(cinux::mm::AddressSpace& space, const cinux::mm::AddressSpace& parent,
                     unsigned long stack_va, unsigned long index) {
    const unsigned long kParentFrame = parent.translate(stack_va);
    if (kParentFrame == 0) {
        return false;
    }
    const auto kFrame = cinux::mm::Pmm::self().allocate_page();
    if (kFrame.raw == 0) {
        return false;
    }
    const auto* const source = reinterpret_cast<const unsigned char*>(kParentFrame);
    auto* const       target = reinterpret_cast<unsigned char*>(kFrame.raw);
    for (unsigned long offset = 0; offset < cinux::arch::page::kSize; ++offset) {
        target[offset] = source[offset];
    }
    *reinterpret_cast<unsigned long*>(kFrame.raw + cinux::arch::page::kSize - 4) = index;
    if (!space.map_user(stack_va, kFrame.raw)) {
        return false;
    }
    cinux::mm::cow::ReleaseFrame(kParentFrame);  // the clone stops sharing the parent stack
    // With the clone off it, the parent's stack is exclusive again, so
    // put its writable permission back; a read-only exclusive page would
    // fault with nobody to answer.
    return const_cast<cinux::mm::AddressSpace&>(parent).map_user(stack_va, kParentFrame);
}

bool setup_task(unsigned long index, unsigned long code_va, unsigned long stack_va,
                cinux::mm::AddressSpace* parent = nullptr) {
    UserTask& task = g_tasks[index];
    task            = UserTask{};
    task.state       = TaskState::kRunnable;
    task.exit_status = 0;
    task.wait_target   = kTaskCount;
    task.wait_mask     = 0;
    task.wait_deadline = 0;
    task.space     = new cinux::mm::AddressSpace{};
    if (task.space == nullptr || !task.space->init()) {
        return false;
    }
    if (parent != nullptr) {
        if (!cinux::armv7::CloneUserSpace(*parent, *task.space) ||
            !privatize_stack(*task.space, *parent, stack_va, index)) {
            return false;
        }
        task.trap      = cinux::arch::UserTrap{};
        task.trap.lr   = code_va + static_cast<unsigned long>(
                                       reinterpret_cast<std::uintptr_t>(&user_entry_stub) -
                                       reinterpret_cast<std::uintptr_t>(__user_text_start));
        task.trap.sp   = stack_va + cinux::arch::page::kSize;
        task.trap.spsr = 0x10UL;
        return true;
    }
    const auto kCodeFrame  = cinux::mm::Pmm::self().allocate_page();
    const auto kStackFrame = cinux::mm::Pmm::self().allocate_page();
    if (kCodeFrame.raw == 0 || kStackFrame.raw == 0) {
        return false;
    }
    const unsigned long kTextBytes =
        static_cast<unsigned long>(__user_text_end - __user_text_start);
    auto* const code = reinterpret_cast<unsigned char*>(kCodeFrame.raw);
    for (unsigned long offset = 0; offset < kTextBytes; ++offset) {
        code[offset] = __user_text_start[offset];
    }
    if (!task.space->map_user(code_va, kCodeFrame.raw) ||
        !task.space->map_user(stack_va, kStackFrame.raw)) {
        return false;
    }
    if (g_shared_frame != 0 &&
        !cinux::armv7::MapSharedFrame(*task.space, 0x40008000UL, static_cast<unsigned long>(g_shared_frame))) {
        return false;
    }
    // The task's argument waits at the top of its own stack; the copied
    // stub reads it before any C prologue can move sp.
    *reinterpret_cast<unsigned long*>(kStackFrame.raw + cinux::arch::page::kSize - 4) = index;
    const unsigned long kEntry =
        code_va + static_cast<unsigned long>(
                      reinterpret_cast<std::uintptr_t>(&user_entry_stub) -
                      reinterpret_cast<std::uintptr_t>(__user_text_start));
    task.trap      = cinux::arch::UserTrap{};
    task.trap.lr   = kEntry;
    task.trap.sp   = stack_va + cinux::arch::page::kSize;
    task.trap.spsr = 0x10UL;  // user mode
    return true;
}

}  // namespace

extern "C" {
cinux::arch::UserTrap g_user_trap;
}

namespace cinux::armv7 {

bool RepairUserPage(unsigned long address, unsigned long status) {
    // A write to a shared, read-only page gets a private copy: the frame
    // the leaf names is duplicated, this space points at the copy, and
    // this owner leaves the shared ledger.
    const unsigned long kPage = address & ~(cinux::arch::page::kSize - 1UL);
    {
        cinux::mm::KernelTables tables;
        const unsigned long     root = cinux::arch::space::ActiveRoot();
        const unsigned long     frame = cinux::mm::walk::TranslatePage(tables, root, kPage);
        if (frame != 0 && (status & cinux::arch::isr::kDfsrWrite) != 0 &&
            cinux::mm::cow::FrameShares(frame) >= 2) {
            const auto kCopy = cinux::mm::Pmm::self().allocate_page();
            if (kCopy.raw == 0) {
                return false;
            }
            const auto* const source = reinterpret_cast<const unsigned char*>(frame);
            auto* const       target = reinterpret_cast<unsigned char*>(kCopy.raw);
            for (unsigned long index = 0; index < cinux::arch::page::kSize; ++index) {
                target[index] = source[index];
            }
            auto* const table = cinux::mm::walk::EnsureLeafTable(tables, root, kPage,
                                                                 cinux::arch::page::kUser);
            if (table == nullptr) {
                return false;
            }
            table[cinux::arch::page::SlotIndex(kPage, cinux::arch::page::kLeafLevel)] =
                cinux::arch::page::MakePageEntry(
                    kCopy.raw, cinux::arch::page::kUser | cinux::arch::page::kWritable);
            cinux::arch::space::InvalidatePage(kPage);
            cinux::mm::cow::ReleaseFrame(frame);
            cinux::print::Println("[armv7] cow copy at %X", kPage);
            return true;
        }
    }

    // Only the demand-page window is fillable; every other fault (the
    // cross-table probe included) must fall through to the task ending.
    constexpr unsigned long kDemandBase = 0x40009000UL;
    constexpr unsigned long kDemandEnd  = 0x4000A000UL;
    if (address < kDemandBase || address >= kDemandEnd) {
        return false;
    }
    cinux::mm::KernelTables tables;
    const unsigned long     root = cinux::arch::space::ActiveRoot();
    if (cinux::mm::walk::TranslatePage(tables, root, kPage) != 0) {
        return false;
    }
    const auto kFrame = cinux::mm::Pmm::self().allocate_page();
    if (kFrame.raw == 0) {
        return false;
    }
    auto* const table = cinux::mm::walk::EnsureLeafTable(tables, root, kPage,
                                                         cinux::arch::page::kUser);
    if (table == nullptr) {
        return false;
    }
    table[cinux::arch::page::SlotIndex(kPage, cinux::arch::page::kLeafLevel)] =
        cinux::arch::page::MakePageEntry(kFrame.raw,
                                         cinux::arch::page::kUser | cinux::arch::page::kWritable);
    cinux::arch::space::InvalidatePage(kPage);
    cinux::print::Println("[armv7] fault repaired at %X", kPage);
    return true;
}

__attribute__((noinline)) bool RunUserModeSelfCheck() {
    InstallFaultRepair(&RepairUserPage);
    const auto kShared = cinux::mm::Pmm::self().allocate_page();
    if (kShared.raw == 0) {
        return false;
    }
    g_shared_frame = kShared.raw;
    g_kernel_root = cinux::arch::space::ActiveRoot();
    if (!setup_task(0, kTaskACode, kTaskAStack)) {
        return false;
    }
    if (!setup_task(1, kTaskACode, kTaskAStack, g_tasks[0].space)) {
        return false;
    }
    if (!setup_task(2, kTaskACode, kTaskAStack, g_tasks[0].space)) {
        return false;
    }
    if (!setup_task(3, kTaskACode, kTaskAStack, g_tasks[0].space)) {
        return false;
    }
    g_running = true;
    g_current = 0;
    cinux::arch::per_cpu::self().slice_ticks = 1;
    cinux::print::Println("[armv7] task 0 starts on its own table");
    cinux::arch::space::SwitchRoot(g_tasks[0].space->root());
    cinux::arch::EnterUserModeWithReturn(g_tasks[0].trap.lr, g_tasks[0].trap.sp, &g_main);
after_drop:
    cinux::mm::cow::ReleaseFrame(g_shared_frame);
    cinux::arch::space::SwitchRoot(g_kernel_root);
    bool all_done = true;
    for (const auto& task : g_tasks) {
        all_done = all_done && task.state == TaskState::kDone;
    }
    return all_done;
}

}  // namespace cinux::armv7

namespace {

/// Picks the next runnable task and hands its frame back to the stub.
/// Never returns when nothing is runnable: it resumes the kernel instead.
/// Wakes the tasks blocked on `target`; each one re-asks on return.
void wake_waiters_of(unsigned long target) {
    for (auto& task : g_tasks) {
        if (task.state == TaskState::kBlocked && ((task.wait_mask >> target) & 1UL) != 0) {
            task.state         = TaskState::kRunnable;
            task.wait_target   = kTaskCount;
            task.wait_mask     = 0;
            task.wait_deadline = 0;
            cinux::print::Println("[armv7] wake a waiter of %u", target);
        }
    }
}

/// Returns one task's space to the allocator. Called only after the
/// scheduler has switched away from it, so its tables are not active.
void teardown_space(unsigned long index) {
    if (g_tasks[index].space == nullptr) {
        return;
    }
    g_tasks[index].space->teardown();
    delete g_tasks[index].space;
    g_tasks[index].space = nullptr;
}

/// The next runnable task after `from`, or kTaskCount when there is none.
unsigned long next_runnable(unsigned long from) {
    for (unsigned long step = 1; step < kTaskCount; ++step) {
        const unsigned long candidate = (from + step) % kTaskCount;
        if (g_tasks[candidate].state == TaskState::kRunnable) {
            return candidate;
        }
    }
    return kTaskCount;
}

cinux::arch::UserTrap* schedule_next(unsigned long number) {
    cinux::arch::UserTrap& running = g_tasks[g_current].trap;
    for (unsigned long word = 0; word < 16; ++word) {
        (&running.r0)[word] = (&g_user_trap.r0)[word];
    }
    if (number == cinux::armv7::syscall_nr::kExit) {
        g_tasks[g_current].state       = TaskState::kDone;
        g_tasks[g_current].exit_status = g_user_trap.r0;
        cinux::print::Println("[armv7] task %u exited with status %u", g_current,
                              g_user_trap.r0);
        wake_waiters_of(g_current);
    }

    if (number == cinux::armv7::syscall_nr::kSleep) {
        const unsigned long kTicks = g_user_trap.r0;
        if (kTicks == 0) {
            return &g_tasks[g_current].trap;
        }
        g_tasks[g_current].state         = TaskState::kBlocked;
        g_tasks[g_current].wait_mask     = 0;
        g_tasks[g_current].wait_target   = kTaskCount;
        g_tasks[g_current].wait_deadline = g_tick_now + kTicks;
        g_tasks[g_current].trap.r0       = 0;
        cinux::print::Println("[armv7] task %u sleeps %u ticks", g_current, kTicks);
    }

    if (number == cinux::armv7::syscall_nr::kWait) {
        const unsigned long kMask = g_user_trap.r0;
        unsigned long       kDone = kTaskCount;
        for (unsigned long index = 0; index < kTaskCount; ++index) {
            if (((kMask >> index) & 1UL) != 0 && g_tasks[index].state == TaskState::kDone) {
                kDone = index;
                break;
            }
        }
        if (kDone != kTaskCount) {
            const unsigned long kStatus = g_tasks[kDone].exit_status;
            if (cinux::arch::CopyToUser(g_user_trap.r1, &kStatus, sizeof(kStatus))) {
                cinux::print::Println("[armv7] wait %u -> %X", kDone, kStatus);
                g_tasks[g_current].trap.r0 = kDone;
            } else {
                g_tasks[g_current].trap.r0 = ~0UL;
            }
            return &g_tasks[g_current].trap;
        }
        // Not finished: park this task; when someone ends, wake_blocked
        // makes it runnable again and it re-asks with -1 in hand.
        const unsigned long kTimeout =
            g_user_trap.r2 == 0 ? kWaitTicks : g_user_trap.r2;
        g_tasks[g_current].state         = TaskState::kBlocked;
        g_tasks[g_current].wait_mask     = kMask;
        g_tasks[g_current].wait_deadline = g_tick_now + kTimeout;
        g_tasks[g_current].trap.r0       = ~0UL;
        cinux::print::Println("[armv7] task %u blocks on %X", g_current, kMask);
    }

    const unsigned long kOld  = g_current;
    const unsigned long kNext = next_runnable(g_current);
    if (kNext != kTaskCount) {
        g_current = kNext;
        cinux::print::Println("[armv7] switch to task %u", kNext);
        cinux::arch::space::SwitchRoot(g_tasks[kNext].space->root());
        if (g_tasks[kOld].state == TaskState::kDone) {
            teardown_space(kOld);
        }
        cinux::arch::per_cpu::self().slice_ticks = 1;
        return &g_tasks[kNext].trap;
    }

    if (g_tasks[g_current].state == TaskState::kRunnable) {
        // The others finished; this task keeps running.
        return &g_tasks[g_current].trap;
    }

    // Nothing runnable but someone still waits: wake them as expired so
    // they can observe the timeout instead of hanging forever.
    bool woke = false;
    for (auto& task : g_tasks) {
        if (task.state == TaskState::kBlocked) {
            task.state         = TaskState::kRunnable;
            task.trap.r0       = task.wait_mask == 0 ? 0 : static_cast<unsigned long>(-2);
            task.wait_mask     = 0;
            task.wait_deadline = 0;
            woke               = true;
        }
    }
    if (woke) {
        const unsigned long kRetry = next_runnable(g_current);
        if (kRetry != kTaskCount) {
            g_current = kRetry;
            cinux::print::Println("[armv7] switch to task %u", kRetry);
            cinux::arch::space::SwitchRoot(g_tasks[kRetry].space->root());
            cinux::arch::per_cpu::self().slice_ticks = 1;
            return &g_tasks[kRetry].trap;
        }
    }

    g_running = false;
    cinux::print::Println("[armv7] all user tasks done");
    cinux::arch::space::SwitchRoot(g_kernel_root);
    teardown_space(kOld);
    cinux::arch::ContextSwitch(&g_scratch, &g_main);
    __builtin_unreachable();
}

}  // namespace

extern "C" cinux::arch::UserTrap* c_schedule_from_user(unsigned long number) {
    return schedule_next(number);
}

extern "C" void SchedulerOnTick(unsigned long ticks) {
    g_tick_now = ticks;
    for (auto& task : g_tasks) {
        if (task.state == TaskState::kBlocked && task.wait_deadline <= ticks) {
            task.state         = TaskState::kRunnable;
            task.wait_target   = kTaskCount;
            const unsigned long kIndex = &task - &g_tasks[0];
            if (task.wait_mask == 0) {
                task.trap.r0 = 0;
                cinux::print::Println("[armv7] task %u woke from sleep", kIndex);
            } else {
                task.trap.r0 = static_cast<unsigned long>(-2);
                cinux::print::Println("[armv7] task %u wait timed out", kIndex);
            }
            task.wait_mask = 0;
        }
    }
}

extern "C" unsigned long CurrentTaskIndex() {
    return g_current;
}

extern "C" bool SchedulerRunning() {
    return g_running;
}

extern "C" bool TaskIsDone(unsigned long index) {
    return index < kTaskCount && g_tasks[index].state == TaskState::kDone;
}

extern "C" unsigned long TaskExitStatus(unsigned long index) {
    if (index >= kTaskCount || g_tasks[index].state != TaskState::kDone) {
        return 0;
    }
    return g_tasks[index].exit_status;
}

extern "C" cinux::arch::UserTrap* c_preempt_from_irq(cinux::arch::UserTrap* trap) {
    if (!g_running) {
        return trap;
    }
    cinux::arch::UserTrap& running = g_tasks[g_current].trap;
    for (unsigned long word = 0; word < 16; ++word) {
        (&running.r0)[word] = (&trap->r0)[word];
    }
    const unsigned long kNext = next_runnable(g_current);
    if (kNext == kTaskCount) {
        return trap;
    }
    g_current = kNext;
    cinux::print::Println("[armv7] preempt to task %u", kNext);
    cinux::arch::space::SwitchRoot(g_tasks[kNext].space->root());
    cinux::arch::per_cpu::self().slice_ticks = 1;
    return &g_tasks[kNext].trap;
}

extern "C" void ResumeTrap(cinux::arch::UserTrap* frame);

/// A user task died on an abort: mark it, run the other one, or go home.
extern "C" [[noreturn]] void c_user_faulted() {
    g_tasks[g_current].state       = TaskState::kDone;
    g_tasks[g_current].exit_status = 0xDEADUL;
    cinux::print::Println("[armv7] task %u faulted", g_current);
    wake_waiters_of(g_current);
    const unsigned long kOld  = g_current;
    const unsigned long kNext = next_runnable(g_current);
    if (kNext != kTaskCount) {
        g_current = kNext;
        cinux::print::Println("[armv7] switch to task %u", kNext);
        cinux::arch::space::SwitchRoot(g_tasks[kNext].space->root());
        teardown_space(kOld);
        asm volatile("cps #0x13" ::: "memory");
        ResumeTrap(&g_tasks[kNext].trap);
    }
    g_running = false;
    cinux::print::Println("[armv7] all user tasks done");
    cinux::arch::space::SwitchRoot(g_kernel_root);
    teardown_space(kOld);
    asm volatile("cps #0x13" ::: "memory");
    cinux::arch::ContextSwitch(&g_scratch, &g_main);
    __builtin_unreachable();
}
