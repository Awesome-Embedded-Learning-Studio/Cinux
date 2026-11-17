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
extern "C" cinux::arch::UserTrap* c_prefetch_trap(cinux::arch::UserTrap* trap);

void HandleDataAbort();

extern "C" {
/// Set by the vector stub each episode; declared in user_mode.cpp.
extern cinux::arch::UserTrap* g_current_trap;
}

extern "C" cinux::arch::UserTrap* c_trap_entry(cinux::arch::UserTrap* trap, unsigned long is_svc) {
    g_current_trap = trap;
    if (is_svc == 2) {
        return c_abort_trap(trap);
    }
    if (is_svc == 3) {
        return c_prefetch_trap(trap);
    }
    if (is_svc != 0) {
        return c_svc_dispatch(trap);
    }
    return c_irq_trap(trap);
}

extern "C" void c_kernel_prefetch_dump(cinux::arch::UserTrap* trap) {
    unsigned long ifar = 0;
    asm volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(ifar));
    // The banked lr of the faulted context names the instruction after
    // the jump: the one address that says which body called into zero.
    cinux::print::Println("[armv7] kernel prefetch abort at %X spsr %X lr %X", ifar,
                          trap->spsr, trap->lr);
    cinux::print::Println("  r0 %X r1 %X r2 %X r3 %X r4 %X", trap->r0, trap->r1, trap->r2,
                          trap->r3, trap->r4);
    cinux::print::Println("  r5 %X r6 %X r7 %X r8 %X r9 %X", trap->r5, trap->r6, trap->r7,
                          trap->r8, trap->r9);
    cinux::print::Println("  r10 %X r11 %X r12 %X sp %X", trap->r10, trap->r11, trap->r12,
                          trap->sp);
    const unsigned long* const kWords = reinterpret_cast<unsigned long*>(trap->sp & ~3UL);
    for (unsigned i = 0; i < 48; ++i) {
        cinux::print::Println("  stack[%u] %X", i, kWords[i]);
    }
    cinux::arch::Halt();
}

extern "C" void c_exception_dispatch(unsigned long vector, unsigned long pc) {
    if (vector == 6) {
        HandleIrq();
        return;
    }
    if (vector == cinux::arch::isr::kPageFaultVector) {
        HandleDataAbort();
        return;
    }
    cinux::print::Println("[armv7] exception %u unhandled, stopping pc %X", vector, pc);
    cinux::arch::Halt();
}
