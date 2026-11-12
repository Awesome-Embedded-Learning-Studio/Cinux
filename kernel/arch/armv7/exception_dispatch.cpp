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

extern "C" cinux::arch::UserTrap* c_schedule_from_user(unsigned long number);
void HandleIrq();

void HandleDataAbort();

extern "C" cinux::arch::UserTrap* c_trap_entry(cinux::arch::UserTrap* trap, unsigned long is_svc) {
    return c_schedule_from_user(trap->r7);
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
