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
#include "kernel/arch/armv7/user_entry.hpp"

void InstallUserReturnContext(cinux::arch::CpuContext* context);

namespace {

constexpr unsigned long kSvcProbe = 0x1337UL;
constexpr unsigned long kSvcExit  = 0x1338UL;

alignas(8) unsigned char       g_user_stack[4096];
cinux::arch::CpuContext        g_kernel_return;
volatile bool                  g_user_exited = false;

extern "C" [[noreturn]] void user_program() {
    cinux::armv7::UartWrite("[armv7] user mode ok\n");

    register unsigned long r7 asm("r7") = kSvcProbe;
    register unsigned long answer asm("r0") = 0;
    asm volatile("svc #0" : "+r"(answer) : "r"(r7) : "memory");
    if (answer == 0x5A5AUL) {
        cinux::armv7::UartWrite("[armv7] svc answer matched\n");
    } else {
        cinux::armv7::UartWrite("[armv7] svc answer WRONG\n");
    }

    register unsigned long r7_exit asm("r7") = kSvcExit;
    asm volatile("svc #0" : : "r"(r7_exit) : "memory");
    for (;;) {
        asm volatile("wfi");
    }
}

}  // namespace

/// Called by the svc-exit path just before resuming the kernel.
extern "C" void NoteUserExit() {
    g_user_exited = true;
}

namespace cinux::armv7 {

bool RunUserModeSelfCheck() {
    g_user_exited = false;
    InstallUserReturnContext(&g_kernel_return);
    const unsigned long kStackTop =
        (reinterpret_cast<unsigned long>(g_user_stack) + sizeof(g_user_stack)) & ~7UL;
    cinux::arch::EnterUserModeWithReturn(reinterpret_cast<unsigned long>(&user_program), kStackTop,
                                         &g_kernel_return);
    return g_user_exited;
}

}  // namespace cinux::armv7
