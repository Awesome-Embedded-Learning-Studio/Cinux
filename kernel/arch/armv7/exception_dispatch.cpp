/**
 * @file    exception_dispatch.cpp
 * @brief   The one C entry the trapping vectors reach.
 *
 * IRQ and the trapping supervisor call both arrive here with a full user
 * frame already saved. The interrupt path serves the timer and returns
 * the frame it was given; the supervisor call path asks the scheduler
 * which frame to resume. The remaining slots report and stop.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.2
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/halt.hpp"
#include "kernel/arch/isr.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/boot/print.hpp"

extern "C" cinux::arch::UserTrap* c_svc_dispatch(cinux::arch::UserTrap* trap);
void HandleIrq();
extern "C" cinux::arch::UserTrap* c_irq_trap(cinux::arch::UserTrap* trap);
extern "C" cinux::arch::UserTrap* c_abort_trap(cinux::arch::UserTrap* trap);

void HandleDataAbort();

extern "C" cinux::arch::UserTrap* c_trap_entry(cinux::arch::UserTrap* trap, unsigned long is_svc) {
    if (is_svc == 2) {
        return c_abort_trap(trap);
    }
    if (is_svc != 0) {
        return c_svc_dispatch(trap);
    }
    return c_irq_trap(trap);
}

extern "C" void c_exception_dispatch(unsigned long vector) {
    if (vector == 6) {
        HandleIrq();
        return;
    }
    if (vector == cinux::arch::isr::kPageFaultVector) {
        HandleDataAbort();
        return;
    }
    if (vector == 3) {
        unsigned long ifar = 0;
        asm volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));  // IFAR
        cinux::print::Println("[armv7] prefetch abort at %X", ifar);
    }
    cinux::print::Println("[armv7] exception %u unhandled, stopping", vector);
    cinux::arch::Halt();
}
