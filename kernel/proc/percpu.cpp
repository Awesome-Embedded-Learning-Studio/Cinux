/**
 * @file kernel/proc/percpu.cpp
 * @brief Per-CPU control blocks (F4-M3 Phase 1).
 *
 * P1-1 keeps single-core semantics: `percpu()` returns `&percpu_blocks[0]` and
 * GS is untouched.  The syscall stack is mirrored to the GS data page
 * registered by `usermode_init` via `set_gs_mirror()`.  P1-2 collapses the GS
 * page into the percpu block and makes `percpu()` read `MSR_GS_BASE`.
 */

#include "kernel/proc/percpu.hpp"

#include "kernel/proc/process.hpp"  // Task (complete type for percpu_blocks)

namespace cinux::proc {

alignas(4096) PerCpu percpu_blocks[kMaxCpus];

namespace {
/// Address of the GS data page syscall_entry reads (`%gs:0`).  Transitional:
/// set by `usermode_init` in P1-1, removed in P1-2 once
/// `KERNEL_GS_BASE == &percpu_blocks[0]`.
uint64_t g_gs_mirror_vaddr = 0;
}  // namespace

PerCpu* percpu() {
    return &percpu_blocks[0];  // P1-1: single core; P1-2 reads MSR_GS_BASE
}

void set_gs_mirror(uint64_t gs_page_vaddr) {
    g_gs_mirror_vaddr = gs_page_vaddr;
}

uint64_t gs_mirror_vaddr() {
    return g_gs_mirror_vaddr;
}

void update_syscall_stack(uint64_t stack_top) {
    percpu()->kernel_stack = stack_top;
    // P1-1: syscall still reads %gs:0 from the separately-allocated GS page
    // registered by usermode_init.  Keep it in sync until P1-2 repoints GS at
    // the percpu block directly.
    if (g_gs_mirror_vaddr != 0) {
        *reinterpret_cast<volatile uint64_t*>(g_gs_mirror_vaddr) = stack_top;
    }
}

}  // namespace cinux::proc
