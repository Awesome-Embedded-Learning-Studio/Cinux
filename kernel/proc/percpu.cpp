/**
 * @file kernel/proc/percpu.cpp
 * @brief Per-CPU control blocks (F4-M3 Phase 1).
 *
 * P1-2: GS points at this CPU's control block throughout kernel mode. Raw
 * `percpu()` remains for IRQ-off/bootstrap code; preemptible paths use one
 * GS-relative instruction per field access so migration cannot stale a pointer.
 */

#include "kernel/proc/percpu.hpp"

#include "kernel/arch/x86_64/msr.hpp"

namespace cinux::proc {

alignas(4096) PerCpu percpu_blocks[kMaxCpus];

PerCpu* percpu() {
    // MSR_GS_BASE == &percpu_blocks[cpu] in kernel mode (swapgs discipline).
    return reinterpret_cast<PerCpu*>(cinux::arch::read_msr(cinux::arch::kMsrGsBase));
}

Task* current_task() {
    Task* task;
    __asm__ volatile("movq %%gs:%c1, %0" : "=r"(task) : "i"(offsetof(PerCpu, current)) : "memory");
    return task;
}

void set_current_task(Task* task) {
    __asm__ volatile("movq %0, %%gs:%c1" : : "r"(task), "i"(offsetof(PerCpu, current)) : "memory");
}

uint32_t current_cpu_id() {
    uint32_t cpu_id;
    __asm__ volatile("movl %%gs:%c1, %0" : "=r"(cpu_id) : "i"(offsetof(PerCpu, cpu_id)) : "memory");
    return cpu_id;
}

void request_reschedule() {
    __asm__ volatile("movb $1, %%gs:%c0" : : "i"(offsetof(PerCpu, need_resched)) : "memory");
}

void clear_reschedule() {
    __asm__ volatile("movb $0, %%gs:%c0" : : "i"(offsetof(PerCpu, need_resched)) : "memory");
}

bool consume_reschedule() {
    uint8_t requested = 0;
    __asm__ volatile("xchgb %0, %%gs:%c1"
                     : "+q"(requested)
                     : "i"(offsetof(PerCpu, need_resched))
                     : "memory");
    return requested != 0;
}

int local_preempt_count() {
    int count;
    __asm__ volatile("movl %%gs:%c1, %0"
                     : "=r"(count)
                     : "i"(offsetof(PerCpu, preempt_count))
                     : "memory");
    return count;
}

void preempt_disable() {
    __asm__ volatile("incl %%gs:%c0" : : "i"(offsetof(PerCpu, preempt_count)) : "memory", "cc");
}

void preempt_enable() {
    __asm__ volatile("decl %%gs:%c0" : : "i"(offsetof(PerCpu, preempt_count)) : "memory", "cc");
}

void update_syscall_stack(uint64_t stack_top) {
    __asm__ volatile("movq %0, %%gs:%c1"
                     :
                     : "r"(stack_top), "i"(offsetof(PerCpu, kernel_stack))
                     : "memory");
}

}  // namespace cinux::proc
