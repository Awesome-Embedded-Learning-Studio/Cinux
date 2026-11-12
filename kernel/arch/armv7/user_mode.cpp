/**
 * @file    user_mode.cpp
 * @brief   Two user tasks, each with its own first-level table.
 *
 * Both tasks run the same copied program; what differs is the table the
 * MMU walks. The program writes through svc, then probes a fixed user
 * address: under its own table the probe lands on mapped code, under the
 * other task's table it is unmapped and must fault. That difference is
 * the point of the round.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.4
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include <cstdint>

#include "kernel/arch/armv7/context.hpp"
#include "kernel/arch/armv7/syscall_numbers.hpp"
#include "kernel/arch/armv7/user_entry.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/page_walk.hpp"
#include "kernel/arch/space_control.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vmm.hpp"

extern "C" unsigned char __user_text_start[];
extern "C" unsigned char __user_text_end[];
extern "C" bool UserExited();
void InstallUserReturnContext(cinux::arch::CpuContext* context);

namespace {

constexpr unsigned long kSharedProbe = 0x40001000UL;
constexpr unsigned long kTaskACode    = 0x40001000UL;
constexpr unsigned long kTaskAStack   = 0x40002000UL;
constexpr unsigned long kTaskBCode    = 0x40003000UL;
constexpr unsigned long kTaskBStack   = 0x40004000UL;

cinux::arch::CpuContext g_kernel_return;

long user_syscall0(unsigned long number) {
    register unsigned long r7 asm("r7") = number;
    register unsigned long r0 asm("r0") = 0;
    asm volatile("svc #0" : "+r"(r0) : "r"(r7) : "memory");
    return static_cast<long>(r0);
}

long user_syscall3(unsigned long number, unsigned long arg0, unsigned long arg1,
                   unsigned long arg2) {
    register unsigned long r7 asm("r7") = number;
    register unsigned long r0 asm("r0") = arg0;
    register unsigned long r1 asm("r1") = arg1;
    register unsigned long r2 asm("r2") = arg2;
    asm volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return static_cast<long>(r0);
}

/// Runs on a user page; reaches the kernel only through svc.
extern "C" [[noreturn]] __attribute__((section(".user_text"))) void user_program() {
    char          buffer[64];
    unsigned long length = 0;
#define PUSH_CHAR(c) buffer[length++] = (c)
    PUSH_CHAR('['); PUSH_CHAR('a'); PUSH_CHAR('r'); PUSH_CHAR('m'); PUSH_CHAR('v');
    PUSH_CHAR('7'); PUSH_CHAR(']'); PUSH_CHAR(' '); PUSH_CHAR('u'); PUSH_CHAR('s');
    PUSH_CHAR('e'); PUSH_CHAR('r'); PUSH_CHAR(' '); PUSH_CHAR('w'); PUSH_CHAR('r');
    PUSH_CHAR('i'); PUSH_CHAR('t'); PUSH_CHAR('e'); PUSH_CHAR(' '); PUSH_CHAR('v');
    PUSH_CHAR('i'); PUSH_CHAR('a'); PUSH_CHAR(' '); PUSH_CHAR('s'); PUSH_CHAR('v');
    PUSH_CHAR('c'); PUSH_CHAR('\n');
#undef PUSH_CHAR
    static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kWrite, 1,
                                    static_cast<unsigned long>(
                                        reinterpret_cast<std::uintptr_t>(buffer)),
                                    length));
    // Under the task's own table this address may be its code; under the
    // other task's table it is unmapped and faults.
    static_cast<void>(*reinterpret_cast<volatile unsigned long*>(kSharedProbe));
    static_cast<void>(user_syscall0(cinux::armv7::syscall_nr::kExit));
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

/// One 16 KiB-aligned page for a first-level table; TTBR0 requires it.
unsigned long allocate_l1_block() {
    // Keep the pages we draw: freeing a rejected page hands it straight
    // back to the next draw, and the loop would never move.
    cinux::base::PhysAddr kept[8];
    unsigned long          kept_count = 0;
    unsigned long          aligned    = 0;
    for (unsigned long attempt = 0; attempt < 8; ++attempt) {
        const auto page = cinux::mm::Pmm::self().allocate_page();
        if (page.raw == 0) {
            break;
        }
        if ((page.raw & 0x3FFFUL) == 0) {
            aligned = page.raw;
            break;
        }
        kept[kept_count++] = page;
    }
    for (unsigned long index = 0; index < kept_count; ++index) {
        cinux::mm::Pmm::self().free_page(kept[index]);
    }
    return aligned;
}

/// Builds one task's root: the kernel's own entries, user window cleared.
unsigned long build_task_root() {
    const unsigned long block = allocate_l1_block();
    if (block == 0) {
        return 0;
    }
    auto* const root = reinterpret_cast<cinux::arch::page::Entry*>(block);
    const auto* const kernel_root =
        reinterpret_cast<const cinux::arch::page::Entry*>(cinux::mm::KernelPageRoot());
    for (unsigned long index = 0; index < 4096; ++index) {
        root[index] = kernel_root[index];
    }
    constexpr unsigned long kUserWindow = 0x40000000UL;
    root[kUserWindow >> 20U] = cinux::arch::page::Entry{0};
    return block;
}

bool run_task(unsigned long root, unsigned long code_va, unsigned long stack_va) {
    const unsigned long kKernelRoot = cinux::arch::space::ActiveRoot();
    cinux::arch::space::SwitchRoot(root);

    const auto kCodeFrame  = cinux::mm::Pmm::self().allocate_page();
    const auto kStackFrame = cinux::mm::Pmm::self().allocate_page();
    if (kCodeFrame.raw == 0 || kStackFrame.raw == 0) {
        cinux::arch::space::SwitchRoot(kKernelRoot);
        return false;
    }
    const unsigned long kTextBytes =
        static_cast<unsigned long>(__user_text_end - __user_text_start);
    auto* const code = reinterpret_cast<unsigned char*>(kCodeFrame.raw);
    for (unsigned long index = 0; index < kTextBytes; ++index) {
        code[index] = __user_text_start[index];
    }
    if (!map_user_page(root, code_va, kCodeFrame.raw) ||
        !map_user_page(root, stack_va, kStackFrame.raw)) {
        cinux::arch::space::SwitchRoot(kKernelRoot);
        return false;
    }

    InstallUserReturnContext(&g_kernel_return);
    const unsigned long kEntry =
        code_va + static_cast<unsigned long>(
                      reinterpret_cast<std::uintptr_t>(&user_program) -
                      reinterpret_cast<std::uintptr_t>(__user_text_start));
    cinux::arch::EnterUserModeWithReturn(kEntry, stack_va + cinux::arch::page::kSize,
                                         &g_kernel_return);
    cinux::arch::space::SwitchRoot(kKernelRoot);
    return UserExited();
}

}  // namespace

namespace cinux::armv7 {

bool RunUserModeSelfCheck() {
    cinux::print::Println("[armv7] task A on its own table");
    if (!run_task(build_task_root(), kTaskACode, kTaskAStack)) {
        return false;
    }
    cinux::print::Println("[armv7] task B on its own table");
    return run_task(build_task_root(), kTaskBCode, kTaskBStack);
}

}  // namespace cinux::armv7
