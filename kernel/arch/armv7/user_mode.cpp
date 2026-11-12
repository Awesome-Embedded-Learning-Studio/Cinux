/**
 * @file    user_mode.cpp
 * @brief   The user-mode round trip: drop, call, exit.
 *
 * One user program runs on a private stack, writes its banner straight
 * to the UART (the device sections are user-reachable on this port),
 * issues one svc with a known number, checks the answer, then exits
 * through a second svc that resumes the kernel.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/context.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/armv7/syscall_numbers.hpp"
#include "kernel/arch/armv7/user_entry.hpp"

void InstallUserReturnContext(cinux::arch::CpuContext* context);

namespace {

long user_syscall3(unsigned long number, unsigned long arg0, unsigned long arg1,
                   unsigned long arg2) {
    register unsigned long r7 asm("r7") = number;
    register unsigned long r0 asm("r0") = arg0;
    register unsigned long r1 asm("r1") = arg1;
    register unsigned long r2 asm("r2") = arg2;
    asm volatile("svc #0" : "+r"(r0) : "r"(r7), "r"(r1), "r"(r2) : "memory");
    return static_cast<long>(r0);
}

alignas(8) unsigned char g_user_stack[4096];
cinux::arch::CpuContext  g_kernel_return;

extern "C" [[noreturn]] void user_program() {
    static const char kMessage[] = "[armv7] user write via svc\n";
    const long        kWritten =
        user_syscall3(cinux::armv7::syscall_nr::kWrite, 1,
                      static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(kMessage)),
                      sizeof(kMessage) - 1);
    cinux::armv7::UartWrite(kWritten > 0 ? "[armv7] svc write matched\n"
                                         : "[armv7] svc write WRONG\n");
    if (user_syscall3(cinux::armv7::syscall_nr::kSchedYield, 0, 0, 0) != 0) {
        cinux::armv7::UartWrite("[armv7] svc yield WRONG\n");
    }
    static_cast<void>(user_syscall3(cinux::armv7::syscall_nr::kExit, 0, 0, 0));
    for (;;) {
        asm volatile("wfi");
    }
}

}  // namespace

extern "C" bool UserExited();

namespace cinux::armv7 {

bool RunUserModeSelfCheck() {
    InstallUserReturnContext(&g_kernel_return);
    const unsigned long kStackTop =
        (reinterpret_cast<unsigned long>(g_user_stack) + sizeof(g_user_stack)) & ~7UL;
    cinux::arch::EnterUserModeWithReturn(reinterpret_cast<unsigned long>(&user_program), kStackTop,
                                         &g_kernel_return);
    return UserExited();
}

}  // namespace cinux::armv7
