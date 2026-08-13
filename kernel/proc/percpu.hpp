/**
 * @file kernel/proc/percpu.hpp
 * @brief Per-CPU control blocks (F4-M3 Phase 1).
 *
 * Replaces the old single global `g_per_cpu` with a per-CPU control block
 * array.  From P1-2 the GS base MSR (`MSR_GS_BASE`) points directly at one
 * block per CPU, so `syscall_entry` reads `kernel_stack` from `%gs:0` and
 * `percpu()` reads `MSR_GS_BASE` to find THIS CPU's block.
 *
 * Swapgs discipline (P1-2): in kernel mode `MSR_GS_BASE` = this CPU's PerCpu
 * block and `MSR_KERNEL_GS_BASE` = the user GS base; `swapgs` on every
 * user<->kernel transition (syscall entry/exit, ISR entry/exit when entered
 * from user mode, and jump_to_usermode) keeps the invariant. Preemptible code
 * uses the GS-relative accessors below so migration cannot split CPU lookup
 * from field access.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cinux::proc {

struct Task;

/// Maximum number of CPUs the kernel supports.  P1-2 keeps single core; only
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
    uint64_t kernel_stack;   ///< @0  syscall %gs:0  — current task kernel stack top
    uint64_t scratch1;       ///< @8  syscall %gs:8  — user RSP scratch
    uint64_t scratch2;       ///< @16 syscall %gs:16 — syscall return value scratch
    Task*    current;        ///< @24 task currently running on this CPU
    uint32_t cpu_id;         ///< @32 logical CPU index (BSP = 0)
    uint32_t apic_id;        ///< @36 Local APIC id
    bool     need_resched;   ///< deferred preempt request set by reschedule IPI / tick
    int      preempt_count;  ///< >0 inside a lock / IRQ-disabled critical section:
                             ///< involuntary preempt is forbidden until it hits 0.
};

static_assert(offsetof(PerCpu, kernel_stack) == 0, "kernel_stack MUST be @0 (syscall %gs:0)");
static_assert(offsetof(PerCpu, scratch1) == 8, "scratch1 @8 (syscall %gs:8)");
static_assert(offsetof(PerCpu, scratch2) == 16, "scratch2 @16 (syscall %gs:16)");
static_assert(offsetof(PerCpu, current) == 24, "current @24");
static_assert(offsetof(PerCpu, cpu_id) == 32, "cpu_id @32");
static_assert(offsetof(PerCpu, need_resched) == 40, "need_resched @40");
static_assert(offsetof(PerCpu, preempt_count) == 44, "preempt_count @44");

/// One control block per CPU, 4 KiB aligned.  Each CPU's GS base points at its
/// own block; P1-2 only uses `[0]` (BSP).
alignas(4096) extern PerCpu percpu_blocks[kMaxCpus];

/// Pointer to THIS CPU's control block, read from `MSR_GS_BASE`.
///
/// A returned pointer must not survive an IRQ-enabled region: an involuntary
/// context switch may migrate the task before its next dereference. Prefer the
/// single-instruction accessors below, or hold preemption disabled.
PerCpu* percpu();

/// Migration-safe, single-instruction GS-relative accessors.
Task*    current_task();
void     set_current_task(Task* task);
uint32_t current_cpu_id();

void request_reschedule();
void clear_reschedule();
bool consume_reschedule();

int  local_preempt_count();
void preempt_disable();
void preempt_enable();

/// Record the current task's kernel stack top into this CPU's PerCpu block.
void update_syscall_stack(uint64_t stack_top);

}  // namespace cinux::proc
