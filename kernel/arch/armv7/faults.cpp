/**
 * @file    faults.cpp
 * @brief   The armv7 fault reporter: say where and why, then stop.
 *
 * A data abort names the faulting address in DFAR and the reason in DFSR
 * (alignment, permission, translation). Bring-up has no recovery, so the
 * report is the whole product: it proves the vector and the abort stack
 * work, and it names the address a future fix-up would look at.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/halt.hpp"

extern "C" [[noreturn]] void c_user_faulted();
#include "kernel/arch/isr.hpp"
#include "kernel/boot/print.hpp"

void HandleDataAbort() {
    unsigned long fault_address = 0;
    unsigned long fault_status  = 0;
    asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(fault_address));  // DFAR
    asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(fault_status));   // DFSR
    unsigned long spsr = 0;
    asm volatile("mrs %0, spsr" : "=r"(spsr));
    const unsigned long long code = cinux::arch::isr::FaultCodeFromStatus(fault_status, spsr);
    if ((spsr & 0x1FUL) == 0x10UL) {
        cinux::print::Println("[armv7] user fault caught at %X code %X", fault_address, code);
        c_user_faulted();
    }
    cinux::print::Println("[armv7] data abort at %X status %X code %X", fault_address, fault_status,
                          code);
    cinux::arch::Halt();
}
