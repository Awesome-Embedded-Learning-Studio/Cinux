/**
 * @file kernel/proc/cpu_context.hpp
 * @brief Callee-saved register snapshot for context switching (CpuContext)
 *
 * Split from process.hpp to keep that file under the 500-line limit.  The
 * layout MUST match context_switch.S exactly; the static_asserts below guard
 * the asm/struct contract.
 *
 * Namespace: cinux::proc
 */

#pragma once

#include <stdint.h>

namespace cinux::proc {

/**
 * @brief Callee-saved register snapshot for task context switch
 *
 * Only the callee-saved registers (r15-r12, rbp, rbx) plus rsp and
 * rip need to be saved/restored because the switch happens at known
 * call boundaries where caller-saved registers are already clobbered.
 *
 * FS base is saved per task (per-thread TLS, MSR_FS_BASE).  rflags stores the
 * outgoing IF state so a task suspended from IRQ exit resumes with interrupts
 * disabled until it unwinds that IRQ frame, while an ordinary task resumes with
 * interrupts enabled.  kgs_base remains reserved: GS is per-CPU and maintained
 * by the swapgs discipline.
 *
 * Layout must match the offsets used in context_switch.S exactly.
 * Note: alignas(16) pads the explicit 88-byte payload to 96 bytes;
 * bytes 88..95 are unused alignment padding (never accessed by asm).
 */
struct alignas(16) CpuContext {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rsp;
    uint64_t rip;
    uint64_t rflags;  ///< Saved RFLAGS subset; context_switch currently restores IF
    uint64_t kgs_base;
    uint64_t fs_base;  ///< Per-thread TLS base (MSR_FS_BASE 0xC0000100), F3-M2
};

static_assert(offsetof(CpuContext, r15) == 0, "r15 at offset 0");
static_assert(offsetof(CpuContext, r14) == 8, "r14 at offset 8");
static_assert(offsetof(CpuContext, r13) == 16, "r13 at offset 16");
static_assert(offsetof(CpuContext, r12) == 24, "r12 at offset 24");
static_assert(offsetof(CpuContext, rbp) == 32, "rbp at offset 32");
static_assert(offsetof(CpuContext, rbx) == 40, "rbx at offset 40");
static_assert(offsetof(CpuContext, rsp) == 48, "rsp at offset 48");
static_assert(offsetof(CpuContext, rip) == 56, "rip at offset 56");
static_assert(offsetof(CpuContext, rflags) == 64, "rflags at offset 64");
static_assert(offsetof(CpuContext, kgs_base) == 72, "kgs_base at offset 72");
static_assert(offsetof(CpuContext, fs_base) == 80, "fs_base at offset 80");
static_assert(sizeof(CpuContext) == 96, "CpuContext must be 96 bytes (alignas(16) pads 88->96)");

}  // namespace cinux::proc
