/**
 * @file    exception_dispatch.cpp
 * @brief   The one C entry every armv7 exception slot reaches.
 *
 * The assembly stubs name their slot and join here; this file decides
 * which handler owns the slot. IRQ and the data abort are the two the
 * bring-up lane services; every other slot reports who fired and stops.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/halt.hpp"
#include "kernel/arch/isr.hpp"
#include "kernel/boot/print.hpp"

void HandleIrq();
void HandleDataAbort();

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
