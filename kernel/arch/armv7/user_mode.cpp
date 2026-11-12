/**
 * @file    user_mode.cpp
 * @brief   The user-mode round trip: copy the user text onto its own page,
 *          map a stack, drop, call, exit.
 *
 * The kernel's own RAM is PL1-only now, so the user program cannot run
 * from where it was linked. Its code lives in .user_text; at run time
 * that section is copied to a freshly mapped user page, and the drop
 * enters the copy. The program reaches the kernel only through svc.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.3
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
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vmm.hpp"
#include "kernel/boot/print.hpp"

extern "C" unsigned char __user_text_start[];
extern "C" unsigned char __user_text_end[];

namespace {

constexpr unsigned long kUserTextPage  = 0x40001000UL;
constexpr unsigned long kUserStackPage = 0x40002000UL;

cinux::arch::CpuContext g_kernel_return;
bool                    g_entered = false;

long user_syscall3(unsigned long number, unsigned long arg0, unsigned long arg1,
                   unsigned long arg2) {
    register unsigned long r7 asm("r7") = number;
    register unsigned long r0 asm("r0") = arg0;
    register unsigned long r1 asm("r1") = arg1;
    register unsigned long r2 asm("r2") = arg2;
    asm volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return static_cast<long>(r0);
}

/// Runs on the user page; reaches the kernel only through svc.
/// The message is built on the user stack so the code carries no
/// absolute reference into the kernel's read-only data.
extern "C" [[noreturn]] __attribute__((section(".user_text"))) void user_program() {
    char                  buffer[64];
    unsigned long         length = 0;
    constexpr const char* kNo = nullptr;
    static_cast<void>(kNo);
#define PUSH_CHAR(c) buffer[length++] = (c)
    PUSH_CHAR('['); PUSH_CHAR('a'); PUSH_CHAR('r'); PUSH_CHAR('m'); PUSH_CHAR('v');
    PUSH_CHAR('7'); PUSH_CHAR(']'); PUSH_CHAR(' '); PUSH_CHAR('u'); PUSH_CHAR('s');
    PUSH_CHAR('e'); PUSH_CHAR('r'); PUSH_CHAR(' '); PUSH_CHAR('w'); PUSH_CHAR('r');
    PUSH_CHAR('i'); PUSH_CHAR('t'); PUSH_CHAR('e'); PUSH_CHAR(' '); PUSH_CHAR('v');
    PUSH_CHAR('i'); PUSH_CHAR('a'); PUSH_CHAR(' '); PUSH_CHAR('s'); PUSH_CHAR('v');
    PUSH_CHAR('c'); PUSH_CHAR('\n');
#undef PUSH_CHAR

    const long kWritten =
        user_syscall3(cinux::armv7::syscall_nr::kWrite, 1,
                      static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(buffer)),
                      length);
    static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kSchedYield, 0, 0, 0));
    // The kernel's own RAM is PL1-only; this read must fault, and the
    // abort handler is expected to bring us back instead of dying.
    static_cast<void>(*reinterpret_cast<volatile unsigned long*>(0x80000000UL));
    for (;;) {
        asm volatile("wfi");
    }
}

bool map_user_page(unsigned long virtual_address, unsigned long physical) {
    cinux::mm::KernelTables tables;
    const unsigned long      root = cinux::arch::space::ActiveRoot();
    cinux::arch::page::Entry* table =
        cinux::mm::walk::EnsureLeafTable(tables, root, virtual_address,
                                         cinux::arch::page::kUser);
    if (table == nullptr) {
        return false;
    }
    table[cinux::arch::page::SlotIndex(virtual_address, cinux::arch::page::kLeafLevel)] =
        cinux::arch::page::MakePageEntry(physical, cinux::arch::page::kUser |
                                                       cinux::arch::page::kWritable);
    cinux::arch::space::InvalidatePage(virtual_address);
    return true;
}

}  // namespace

void InstallUserReturnContext(cinux::arch::CpuContext* context);
extern "C" bool UserExited();

namespace cinux::armv7 {

bool RunUserModeSelfCheck() {
    const unsigned long kTextBytes =
        static_cast<unsigned long>(__user_text_end - __user_text_start);
    const auto kCodeFrame  = cinux::mm::Pmm::self().allocate_page();
    const auto kStackFrame = cinux::mm::Pmm::self().allocate_page();
    if (kCodeFrame.raw == 0 || kStackFrame.raw == 0) {
        return false;
    }
    auto* const code = reinterpret_cast<unsigned char*>(kCodeFrame.raw);
    for (unsigned long index = 0; index < kTextBytes; ++index) {
        code[index] = __user_text_start[index];
    }
    if (!map_user_page(kUserTextPage, kCodeFrame.raw) ||
        !map_user_page(kUserStackPage, kStackFrame.raw)) {
        return false;
    }

    InstallUserReturnContext(&g_kernel_return);
    const unsigned long kEntry =
        kUserTextPage + static_cast<unsigned long>(
                            reinterpret_cast<std::uintptr_t>(&user_program) -
                            reinterpret_cast<std::uintptr_t>(__user_text_start));
    const unsigned long kStackTop = kUserStackPage + cinux::arch::page::kSize;
    cinux::arch::EnterUserModeWithReturn(kEntry, kStackTop, &g_kernel_return);
    return UserExited();
}

}  // namespace cinux::armv7
