/**
 * @file    svc.cpp
 * @brief   The armv7 supervisor-call handler: number in r7, value in r0.
 *
 * Linux's armv7 ABI puts the syscall number in r7 and the arguments in
 * r0 to r3. The vector stub forwards r7 and stores the handler's return
 * value into the saved r0, so the user side sees it as the call's result.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/context.hpp"
#include "kernel/boot/print.hpp"

namespace {

constexpr unsigned long kSvcProbe = 0x1337UL;
constexpr unsigned long kSvcExit  = 0x1338UL;

cinux::arch::CpuContext  g_scratch;
cinux::arch::CpuContext* g_kernel_return = nullptr;

}  // namespace

extern "C" void NoteUserExit();

void InstallUserReturnContext(cinux::arch::CpuContext* context) {
    g_kernel_return = context;
}

extern "C" unsigned long c_svc_handler(unsigned long number) {
    if (number == kSvcProbe) {
        cinux::print::Println("[armv7] svc %X answered %X", number, 0x5A5AUL);
        return 0x5A5AUL;
    }
    if (number == kSvcExit && g_kernel_return != nullptr) {
        cinux::print::Println("[armv7] user exit");
        NoteUserExit();
        cinux::arch::ContextSwitch(&g_scratch, g_kernel_return);
    }
    return ~0UL;
}
