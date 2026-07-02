/**
 * @file kernel/arch/x86_64/exception_handlers.cpp
 * @brief CPU exception handler implementations for the big kernel
 *
 * Provides C handler functions for all configured CPU exceptions (vectors 0-14).
 * Called by ISR stubs in interrupts.S with an InterruptFrame* argument.
 *
 * Policy:
 *   - Non-fatal exceptions (#BP, #DB): print info and continue via IRETQ
 *   - Fatal exceptions (all others): print register dump, then cli;hlt forever
 *
 * Exception-type messages go through klog (klog_error/klog_warn) so they
 * enter the dmesg ring buffer with a level, while still printing in real
 * time.  The verbose register dump and kpanic stay on kprintf (real-time
 * diagnostics; the dump is large and kpanic is the halt path).
 */

#include <stdarg.h>
#include <stdint.h>

#include "arch/x86_64/paging.hpp"
#include "kernel/arch/x86_64/backtrace.hpp"
#include "kernel/arch/x86_64/extable.hpp"  // F-EXTABLE: search_exception_tables
#include "kernel/arch/x86_64/fault_diag.hpp"  // F-VERIFY: capture_first_gp/pf + CoW diag (extracted from this file)
#include "kernel/arch/x86_64/idt.hpp"
#include "kernel/arch/x86_64/io.hpp"
#include "kernel/arch/x86_64/paging_config.hpp"
#include "kernel/arch/x86_64/phys_virt.hpp"
#include "kernel/lib/klog.hpp"
#include "kernel/lib/kprintf.hpp"
#include "kernel/lib/string.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/diagnostics.hpp"
#include "kernel/mm/page_cache.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/mm/vmm.hpp"
#include "kernel/proc/process.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/signal.hpp"

extern "C" char __kernel_stack_top[];
extern "C" char __boot_guard_start[];
extern "C" char __boot_guard_end[];

namespace {

using cinux::arch::InterruptFrame;
using cinux::lib::kprintf;
using cinux::mm::g_vmm;

void dump_registers(const InterruptFrame* frame, const char* name, uint8_t vector) {
    kprintf("\n");
    kprintf("==== EXCEPTION: %s (vector %u) ====\n", name, vector);
    kprintf("  RIP   = %p   CS  = 0x%04x\n", reinterpret_cast<void*>(frame->rip),
            static_cast<unsigned>(frame->cs));
    kprintf("  RFLAGS= %p\n", reinterpret_cast<void*>(frame->rflags));
    kprintf("  RSP   = %p   SS  = 0x%04x\n", reinterpret_cast<void*>(frame->rsp),
            static_cast<unsigned>(frame->ss));
    kprintf("  RAX=%p  RBX=%p\n", reinterpret_cast<void*>(frame->rax),
            reinterpret_cast<void*>(frame->rbx));
    kprintf("  RCX=%p  RDX=%p\n", reinterpret_cast<void*>(frame->rcx),
            reinterpret_cast<void*>(frame->rdx));
    kprintf("  RSI=%p  RDI=%p\n", reinterpret_cast<void*>(frame->rsi),
            reinterpret_cast<void*>(frame->rdi));
    kprintf("  RBP=%p  R8 =%p\n", reinterpret_cast<void*>(frame->rbp),
            reinterpret_cast<void*>(frame->r8));
    kprintf("  R9 =%p  R10=%p\n", reinterpret_cast<void*>(frame->r9),
            reinterpret_cast<void*>(frame->r10));
    kprintf("  R11=%p  R12=%p\n", reinterpret_cast<void*>(frame->r11),
            reinterpret_cast<void*>(frame->r12));
    kprintf("  R13=%p  R14=%p\n", reinterpret_cast<void*>(frame->r13),
            reinterpret_cast<void*>(frame->r14));
    kprintf("  R15=%p\n", reinterpret_cast<void*>(frame->r15));
    kprintf("  ERROR CODE = %p\n", reinterpret_cast<void*>(frame->error_code));
    kprintf("========================================\n");
}

[[noreturn]] void fatal_halt() {
    while (1) {
        __asm__ volatile(
            "cli; \
            hlt");
    }
}

}  // anonymous namespace

// Central kernel-panic path (FO batch 3): uniform diagnostics for every fatal
// exception and explicit kpanic().  Prints the reason, dumps registers when a
// frame is available, walks + symbolizes the call stack, notes the current
// task, then halts.  Replaces the per-handler dump_registers/klog/fatal_halt
// triplet with a single entry point.
[[noreturn]] void panic(const InterruptFrame* frame, const char* name, uint8_t vector,
                        const char* fmt, ...) {
    kprintf("\n========== KERNEL PANIC ==========\n");
    va_list args;
    va_start(args, fmt);
    cinux::lib::kvprintf(fmt, args);
    va_end(args);
    kprintf("\n");

    if (frame != nullptr) {
        dump_registers(frame, name, vector);
        cinux::arch::backtrace_from(frame->rbp);
    } else {
        cinux::arch::backtrace();
    }

    if (auto* t = cinux::proc::Scheduler::current(); t != nullptr) {
        kprintf("Task: tid=%u pid=%d name='%s'\n", static_cast<unsigned>(t->tid), t->pid,
                t->name ? t->name : "(null)");
    }
    cinux::mm::dump_memory_stats();
    kprintf("==================================\n");
    // isa-debug-exit (port 0xf4): terminate QEMU immediately with a code that
    // names the cause, so CI fails FAST instead of hanging on cli;hlt until the
    // timeout kills QEMU (the old "PF-killed" 2-minute stall). Encoding:
    // value = vector + 2 (value 0=success and 1=test-fail are reserved; value 128
    // also collides with success via (v<<1|1) mod 256, so stay < 128). QEMU exits
    // (value<<1)|1: #DE(0)->5, #DF(8)->21, #GP(13)->31, #PF(14)->33. No-op on bare
    // metal / the production `run` target (no isa-debug-exit device there) --
    // fatal_halt() is the fallback.
    __asm__ volatile("outl %0, $0xf4" : : "a"(static_cast<uint32_t>(vector + 2)));
    fatal_halt();
}

// ============================================================
// Exception handlers (C-linkage, called from ISR stubs)
// ============================================================

extern "C" {

void handle_db(InterruptFrame* frame) {
    dump_registers(frame, "#DB", 1);
    klog_warn("Debug exception (#DB), continuing");
}

void handle_bp(InterruptFrame* frame) {
    dump_registers(frame, "#BP", 3);
    klog_warn("Breakpoint (#BP) at RIP=%p", reinterpret_cast<void*>(frame->rip));
    klog_warn("Continuing");
}

void handle_de(InterruptFrame* frame) {
    panic(frame, "#DE", 0, "Divide Error");
}

void handle_nmi(InterruptFrame* frame) {
    panic(frame, "NMI", 2, "Non-maskable Interrupt");
}

void handle_of(InterruptFrame* frame) {
    panic(frame, "#OF", 4, "Overflow");
}

void handle_br(InterruptFrame* frame) {
    panic(frame, "#BR", 5, "BOUND Range Exceeded");
}

void handle_ud(InterruptFrame* frame) {
    panic(frame, "#UD", 6, "Invalid Opcode");
}

void handle_nm(InterruptFrame* frame) {
    panic(frame, "#NM", 7, "Device Not Available");
}

void handle_df(InterruptFrame* frame) {
    panic(frame, "#DF", 8, "Double Fault (error code=%p)",
          reinterpret_cast<void*>(frame->error_code));
}

void handle_ts(InterruptFrame* frame) {
    panic(frame, "#TS", 10, "Invalid TSS (error code=%p)",
          reinterpret_cast<void*>(frame->error_code));
}

void handle_np(InterruptFrame* frame) {
    panic(frame, "#NP", 11, "Segment Not Present (error code=%p)",
          reinterpret_cast<void*>(frame->error_code));
}

void handle_ss(InterruptFrame* frame) {
    panic(frame, "#SS", 12, "Stack Fault (error code=%p)",
          reinterpret_cast<void*>(frame->error_code));
}

void handle_gp(InterruptFrame* frame) {
    // F4-M4 M4-2-3 (GOTCHA#25): capture the FIRST faulting frame to debug.log
    // before panic()/current() can recurse on a corrupt %gs. See capture_first_gp.
    capture_first_gp(frame);
    const bool from_user = (frame->cs & 0x03) != 0;
    auto*      task      = cinux::proc::Scheduler::current();
    if (from_user && task != nullptr) {
        // User-mode #GP: an illegal/privileged opcode in ring 3 -- e.g. clang
        // lowering a user-program UB / unreachable path to `hlt` (a 1-byte trap
        // that #GP's in ring 3), or a bad segment selector. The offending USER
        // task must die, NOT the kernel: this aligns with Linux (SIGILL) and the
        // #PF handler's SIGSEGV path below. signal_send queues kSigill; the ISR
        // stub's signal_check_deliver_isr (invoked right after handle_gp returns)
        // delivers it -- to a custom handler if installed, else the default
        // Terminate (its context_switch() abandons this frame). F-ECO batch 0:
        // without this, every user program UB that clang lowers to `hlt` would
        // panic the whole kernel (busybox echo hit this on iter 1).
        uint64_t cr2;
        __asm__ volatile("movq %%cr2, %0" : "=r"(cr2));
        klog_error("#GP user mode: tid=%u '%s' rip=%p cs=%p rsp=%p err=%p cr2=%p -- sending SIGILL",
                   static_cast<unsigned>(task->tid), task->name ? task->name : "(null)",
                   reinterpret_cast<void*>(frame->rip), reinterpret_cast<void*>(frame->cs),
                   reinterpret_cast<void*>(frame->rsp), reinterpret_cast<void*>(frame->error_code),
                   reinterpret_cast<void*>(cr2));
        cinux::proc::signal_send(task, cinux::proc::Signal::kSigill);
        return;
    }
    if (from_user) {
        panic(frame, "#GP", 13, "General Protection Fault from user mode (no current task)");
    }
    panic(frame, "#GP", 13, "General Protection Fault in kernel mode (error code=%p)",
          reinterpret_cast<void*>(frame->error_code));
}

}  // extern "C"
