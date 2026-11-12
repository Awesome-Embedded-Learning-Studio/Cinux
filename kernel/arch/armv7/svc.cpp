/**
 * @file    svc.cpp
 * @brief   The armv7 supervisor-call dispatch: number in r7, args in r0-r3.
 *
 * The vector stub hands over the number and a pointer to the saved
 * argument quad. The handler writes its answer back through r0; the stub
 * stores that into the saved slot, so the user side sees it as the call's
 * result. exit never returns: it swaps the kernel context back in.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.2
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/context.hpp"
#include "kernel/arch/armv7/syscall_numbers.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/boot/print.hpp"

namespace {

unsigned long do_write(unsigned long fd, unsigned long buffer, unsigned long count) {
    if (fd != 1 && fd != 2) {
        return static_cast<unsigned long>(-1);
    }
    const cinux::arch::IrqGuard kGuard;
    const auto* bytes = reinterpret_cast<const char*>(buffer);
    for (unsigned long index = 0; index < count; ++index) {
        cinux::armv7::UartPutChar(bytes[index]);
    }
    return count;
}

}  // namespace

extern "C" unsigned long c_svc_handler(unsigned long number, unsigned long* frame) {
    switch (number) {
    case cinux::armv7::syscall_nr::kWrite: {
        const unsigned long result = do_write(frame[0], frame[1], frame[2]);
        cinux::print::Println("[armv7] svc write %u bytes", result);
        return result;
    }
    case cinux::armv7::syscall_nr::kSchedYield:
        return 0;
    default:
        cinux::print::Println("[armv7] svc %X unknown", number);
        return ~0UL;
    }
}
