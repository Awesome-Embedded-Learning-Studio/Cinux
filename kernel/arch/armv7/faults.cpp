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
#include "kernel/mm/vma.hpp"
#include "kernel/proc/scheduler.hpp"

extern "C" [[noreturn]] void c_user_faulted();
namespace cinux::armv7 {
/// The only user world left is the seated one: a fault the seat could
/// not repair retires the task through the exit door, cleanly.
[[noreturn]] void SeatFaultDeath();
}  // namespace cinux::armv7

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
    // The IFSR has no write marker: its bit 11 means something else
    // entirely, so the status is handed on with that bit cleared. A
    // fetch is a read by definition, and a decoder that read the bit
    // as "this was a write" would refuse to fill a read-only code page
    // the image is perfectly entitled to execute from.
    const unsigned long kStatus = fault_status & ~cinux::arch::isr::kDfsrWrite;
    const unsigned long long code = cinux::arch::isr::FaultCodeFromStatus(kStatus, trap->spsr);
    if (g_repair != nullptr && g_repair(fault_address, kStatus)) {
        return trap;  // the fetch runs again on the new mapping
    }
    cinux::print::Println("[armv7] user fetch fault caught at %X code %X", fault_address, code);
    cinux::armv7::SeatFaultDeath();
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
    cinux::print::Println("[armv7] user fault at %X code %X pid %u pc %X r0 %X r1 %X r2 %X r3 %X",
                          fault_address, code,
                          (unsigned)cinux::proc::Scheduler::self().current()->pid,
                          (unsigned long)trap->lr, (unsigned long)trap->r0,
                          (unsigned long)trap->r1, (unsigned long)trap->r2,
                          (unsigned long)trap->r3);
    {
        // The frame chain names the callers: an ARM frame holds the
        // previous frame pointer then the return address. The chain stops
        // when a link points back up the stack or leaves the user window,
        // instead of chasing a stale word into the kernel's maps.
        unsigned long frame = static_cast<unsigned long>(trap->r11) & ~3UL;
        for (unsigned long depth = 0; depth < 6; ++depth) {
            if (frame < cinux::mm::kUserNullTop || frame >= cinux::mm::kUserMmapTop) {
                break;
            }
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            const auto* const kSlot = reinterpret_cast<const unsigned long*>(frame);
            const unsigned long kNext = kSlot[0];
            const unsigned long kReturn = kSlot[1];
            cinux::print::Println("[armv7] frame %u at %X return %X", (unsigned)depth, frame,
                                  kReturn);
            if (kNext <= frame) {
                break;
            }
            frame = kNext & ~3UL;
        }
    }
    cinux::armv7::SeatFaultDeath();
}
