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
#include "kernel/arch/armv7/per_cpu.hpp"
#include "kernel/arch/armv7/user_entry.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/page_walk.hpp"
#include "kernel/arch/space_control.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/arch/armv7/cow_ledger.hpp"
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

struct UserTask {
    cinux::arch::UserTrap      trap;
    cinux::mm::AddressSpace*   space;
    bool                       done;
};

UserTask                g_tasks[2];
cinux::arch::CpuContext g_main;
cinux::arch::CpuContext g_scratch;
unsigned long           g_kernel_root = 0;
unsigned long           g_current     = 0;
bool                    g_running     = false;
unsigned long long      g_shared_frame = 0;

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
    for (unsigned long round = 0; round < 20; ++round) {
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
    task           = UserTask{};
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

bool RunUserModeSelfCheck() {
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
    g_running = true;
    g_current = 0;
    cinux::arch::per_cpu::self().slice_ticks = 1;
    cinux::print::Println("[armv7] task 0 starts on its own table");
    cinux::arch::space::SwitchRoot(g_tasks[0].space->root());
    cinux::arch::EnterUserModeWithReturn(g_tasks[0].trap.lr, g_tasks[0].trap.sp, &g_main);
    cinux::arch::space::SwitchRoot(g_kernel_root);
    return g_tasks[0].done && g_tasks[1].done;
}

}  // namespace cinux::armv7

namespace {

/// Picks the next runnable task and hands its frame back to the stub.
/// Never returns when nothing is runnable: it resumes the kernel instead.
cinux::arch::UserTrap* schedule_next(unsigned long number) {
    cinux::arch::UserTrap& running = g_tasks[g_current].trap;
    for (unsigned long word = 0; word < 16; ++word) {
        (&running.r0)[word] = (&g_user_trap.r0)[word];
    }
    if (number == cinux::armv7::syscall_nr::kExit) {
        g_tasks[g_current].done = true;
        cinux::print::Println("[armv7] task %u exited with status %u", g_current,
                              g_user_trap.r0);
    }

    const unsigned long other = g_current == 0 ? 1 : 0;
    if (!g_tasks[other].done) {
        g_current = other;
        cinux::print::Println("[armv7] switch to task %u", other);
        cinux::arch::space::SwitchRoot(g_tasks[other].space->root());
        cinux::arch::per_cpu::self().slice_ticks = 1;
        return &g_tasks[other].trap;
    }

    if (!g_tasks[g_current].done) {
        // The other one finished; this task keeps running.
        return &g_tasks[g_current].trap;
    }

    g_running = false;
    cinux::print::Println("[armv7] all user tasks done");
    cinux::arch::space::SwitchRoot(g_kernel_root);
    cinux::arch::ContextSwitch(&g_scratch, &g_main);
    __builtin_unreachable();
}

}  // namespace

extern "C" cinux::arch::UserTrap* c_schedule_from_user(unsigned long number) {
    return schedule_next(number);
}

extern "C" bool SchedulerRunning() {
    return g_running;
}

extern "C" cinux::arch::UserTrap* c_preempt_from_irq(cinux::arch::UserTrap* trap) {
    if (!g_running) {
        return trap;
    }
    cinux::arch::UserTrap& running = g_tasks[g_current].trap;
    for (unsigned long word = 0; word < 16; ++word) {
        (&running.r0)[word] = (&trap->r0)[word];
    }
    const unsigned long other = g_current == 0 ? 1 : 0;
    if (g_tasks[other].done) {
        return trap;
    }
    g_current = other;
    cinux::print::Println("[armv7] preempt to task %u", other);
    cinux::arch::space::SwitchRoot(g_tasks[other].space->root());
    cinux::arch::per_cpu::self().slice_ticks = 1;
    return &g_tasks[other].trap;
}

extern "C" void ResumeTrap(cinux::arch::UserTrap* frame);

/// A user task died on an abort: mark it, run the other one, or go home.
extern "C" [[noreturn]] void c_user_faulted() {
    g_tasks[g_current].done = true;
    cinux::print::Println("[armv7] task %u faulted", g_current);
    const unsigned long other = g_current == 0 ? 1 : 0;
    if (!g_tasks[other].done) {
        g_current = other;
        cinux::print::Println("[armv7] switch to task %u", other);
        cinux::arch::space::SwitchRoot(g_tasks[other].space->root());
        asm volatile("cps #0x13" ::: "memory");
        ResumeTrap(&g_tasks[other].trap);
    }
    g_running = false;
    cinux::print::Println("[armv7] all user tasks done");
    cinux::arch::space::SwitchRoot(g_kernel_root);
    asm volatile("cps #0x13" ::: "memory");
    cinux::arch::ContextSwitch(&g_scratch, &g_main);
    __builtin_unreachable();
}
