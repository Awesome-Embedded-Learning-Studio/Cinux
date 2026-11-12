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
#include "kernel/boot/print.hpp"

extern "C" void c_data_abort_handler() {
    unsigned long fault_address = 0;
    unsigned long fault_status  = 0;
    asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(fault_address));  // DFAR
    asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(fault_status));   // DFSR
    cinux::print::Println("[armv7] data abort at %X status %X", fault_address, fault_status);
    cinux::arch::Halt();
}
