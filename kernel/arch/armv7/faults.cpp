/**
 * @file    faults.cpp
 * @brief   The armv7 fault reporter, with a repair door for user faults.
 *
 * A fault taken in the kernel has nowhere to go: it prints and stops.
 * One taken in user mode arrives with a full trap frame, so it is offered
 * to a repair hook first; a hook that maps the missing page lets the same
 * instruction run again. A refused fault ends the task, as before.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.3
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/halt.hpp"
#include "kernel/arch/isr.hpp"
#include "kernel/arch/armv7/user_trap.hpp"
#include "kernel/boot/print.hpp"

extern "C" [[noreturn]] void c_user_faulted();
extern "C" cinux::arch::UserTrap* c_user_fault_handler(cinux::arch::UserTrap* trap,
                                                       unsigned long address,
                                                       unsigned long code);

using RepairHook = bool (*)(unsigned long address, unsigned long status);

namespace {

RepairHook g_repair = nullptr;

}  // namespace

void InstallFaultRepair(RepairHook hook) {
    g_repair = hook;
}

void HandleDataAbort() {
    unsigned long fault_address = 0;
    unsigned long fault_status  = 0;
    asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(fault_address));  // DFAR
    asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(fault_status));   // DFSR
    unsigned long spsr = 0;
    asm volatile("mrs %0, spsr" : "=r"(spsr));
    const unsigned long long code = cinux::arch::isr::FaultCodeFromStatus(fault_status, spsr);
    cinux::print::Println("[armv7] data abort at %X status %X code %X", fault_address, fault_status,
                          code);
    cinux::arch::Halt();
}

/// A user-mode prefetch abort: the fetch missed, so the demand pager
/// gets the same chance a data miss gets and the instruction runs
/// again on the filled page. The fault words live in the instruction
/// fault registers, not the data ones.
extern "C" cinux::arch::UserTrap* c_prefetch_trap(cinux::arch::UserTrap* trap) {
    unsigned long fault_address = 0;
    unsigned long fault_status  = 0;
    asm volatile("mrc p15, 0, %0, c6, c0, 2" : "=r"(fault_address));  // IFAR
    asm volatile("mrc p15, 0, %0, c5, c0, 1" : "=r"(fault_status));   // IFSR
    const unsigned long long code =
        cinux::arch::isr::FaultCodeFromStatus(fault_status, trap->spsr);
    if (g_repair != nullptr && g_repair(fault_address, fault_status)) {
        return trap;  // the fetch runs again on the new mapping
    }
    if (auto* const kHandled = c_user_fault_handler(trap, fault_address, code);
        kHandled != nullptr) {
        return kHandled;
    }
    cinux::print::Println("[armv7] user fetch fault caught at %X code %X", fault_address,
                          code);
    c_user_faulted();
}

extern "C" cinux::arch::UserTrap* c_abort_trap(cinux::arch::UserTrap* trap) {
    unsigned long fault_address = 0;
    unsigned long fault_status  = 0;
    asm volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(fault_address));  // DFAR
    asm volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(fault_status));   // DFSR
    const unsigned long long code =
        cinux::arch::isr::FaultCodeFromStatus(fault_status, trap->spsr);
    if ((trap->spsr & 0x1FUL) != 0x10UL) {
        cinux::print::Println("[armv7] data abort at %X status %X code %X", fault_address,
                              fault_status, code);
        cinux::arch::Halt();
    }
    if (g_repair != nullptr && g_repair(fault_address, fault_status)) {
        return trap;  // the same instruction runs again on the new mapping
    }
    if (auto* const kHandled = c_user_fault_handler(trap, fault_address, code);
        kHandled != nullptr) {
        return kHandled;  // the handler runs in user mode instead
    }
    cinux::print::Println("[armv7] user fault caught at %X code %X", fault_address, code);
    c_user_faulted();
}
