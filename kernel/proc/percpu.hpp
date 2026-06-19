/**
 * @file kernel/proc/percpu.hpp
 * @brief Per-CPU control blocks (F4-M3 Phase 1).
 *
 * Replaces the old single global `g_per_cpu` with a per-CPU control block
 * array.  Phase 2 will point the GS base MSR directly at one block per CPU so
 * that syscall_entry reads `kernel_stack` from `%gs:0`.
 *
 * P1-1 keeps single-core behaviour: `percpu()` returns `&percpu_blocks[0]`
 * statically and the GS data page allocated by `usermode_init` is unchanged.
 * `update_syscall_stack` mirrors `kernel_stack` into that GS page (registered
 * via `set_gs_mirror`) until P1-2 collapses the GS page into the percpu block.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cinux::proc {

struct Task;

/// Maximum number of CPUs the kernel supports.  P1-1 is single core; only
/// `percpu_blocks[0]` is used until AP boot (Phase 2).
constexpr uint32_t kMaxCpus = 8;

/// Per-CPU control block.
///
/// Layout is a hard contract with `syscall.S`: `kernel_stack` MUST stay at
/// offset 0 because syscall_entry loads the kernel stack from `%gs:0`, and the
/// scratch slots at offset 8/16 back `%gs:8`/`%gs:16`.  The static_asserts
/// below lock these offsets so future field edits cannot silently break
/// syscall.
struct PerCpu {
    uint64_t kernel_stack;  ///< @0  syscall %gs:0  — current task kernel stack top
    uint64_t scratch1;      ///< @8  syscall %gs:8  — user RSP scratch
    uint64_t scratch2;      ///< @16 syscall %gs:16 — syscall return value scratch
    Task*    current;       ///< @24 task currently running on this CPU
    uint32_t cpu_id;        ///< @32 logical CPU index (BSP = 0)
    uint32_t apic_id;       ///< @36 Local APIC id
};

static_assert(offsetof(PerCpu, kernel_stack) == 0, "kernel_stack MUST be @0 (syscall %gs:0)");
static_assert(offsetof(PerCpu, scratch1) == 8, "scratch1 @8 (syscall %gs:8)");
static_assert(offsetof(PerCpu, scratch2) == 16, "scratch2 @16 (syscall %gs:16)");
static_assert(offsetof(PerCpu, current) == 24, "current @24");

/// One control block per CPU, 4 KiB aligned.  In Phase 2 each CPU's GS base
/// points at its block; P1-1 only touches `[0]`.
alignas(4096) extern PerCpu percpu_blocks[kMaxCpus];

/// Pointer to THIS CPU's control block.
/// P1-1: statically returns `&percpu_blocks[0]` (single core, GS untouched).
/// P1-2: reads `MSR_GS_BASE` (== `&percpu_blocks[cpu]` after swapgs).
PerCpu* percpu();

/// P1-1 transitional: register the GS data page that syscall_entry reads via
/// `%gs:0`.  While GS still points at the separately-allocated page (not yet
/// the percpu block), `update_syscall_stack` mirrors `kernel_stack` there.
/// Removed in P1-2 once `KERNEL_GS_BASE` is pointed at `percpu_blocks[0]`.
void     set_gs_mirror(uint64_t gs_page_vaddr);
uint64_t gs_mirror_vaddr();

/// Record the current task's kernel stack top, propagating it to wherever
/// syscall_entry will read it (`%gs:0`).  Called by the scheduler on each
/// context switch and by the user-mode jump-in paths.
void update_syscall_stack(uint64_t stack_top);

}  // namespace cinux::proc
