/**
 * @file    context.hpp
 * @brief   The armv7 register image a cooperative switch saves.
 *
 * A switch happens at a function-call boundary, so only the callee-saved
 * registers travel: r4 through r11, the stack pointer, the return
 * address, and SPSR — without SPSR a switch out of a supervisor call
 * would resume in the wrong mode. Floating point is deliberately out of this image for now; a
 * task that uses VFP will need its own save area, which is the same
 * decision x86 made when it added FXSAVE64.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstddef>

namespace cinux::arch {

/// This lane does not save floating point with a task; the type exists
/// so the shared task block compiles.
struct FpuImage {
    unsigned long long words[64];
};

/// The register image context_switch.S moves with plain offsets.
struct CpuContext {
    unsigned long r4;
    unsigned long r5;
    unsigned long r6;
    unsigned long r7;
    unsigned long r8;
    unsigned long r9;
    unsigned long r10;
    unsigned long r11;
    unsigned long sp;
    unsigned long lr;
    unsigned long spsr;

    /// No segment bases exist on this port; the fields keep the shared
    /// switch code compilable and stay zero.
    unsigned long long fs_base = 0;
    unsigned long long gs_base = 0;
};

static_assert(offsetof(CpuContext, spsr) == 10 * sizeof(unsigned long),
              "the switch moves eleven words");
static_assert(sizeof(CpuContext) == 64, "two address-register words close the context");

/// Saves the running world into `from` and resumes `to` in the same mode.
extern "C" void ContextSwitch(CpuContext* from, const CpuContext* to);

/// Saves `from` and resumes `to` through an exception return: the loaded
/// SPSR picks the mode, so a user task resumes as user.
extern "C" void ContextSwitchIntoUser(CpuContext* from, const CpuContext* to);

}  // namespace cinux::arch

extern "C" {

/// The first instruction of a freshly built task: r4 holds the body and
/// r5 the exit hook, so a body that returns still lands in the exit path.
void TaskTrampoline();

}  // extern "C"
