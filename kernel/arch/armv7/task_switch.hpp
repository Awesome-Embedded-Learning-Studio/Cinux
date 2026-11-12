/**
 * @file    task_switch.hpp
 * @brief   armv7's part of entering a task: a stack top, and nothing else
 *          yet.
 *
 * There is no TSS and no segment base here: the supervisor stack is just
 * the SVC bank's pointer, and per-task address registers would be the
 * thread-id registers, which this lane does not use. The named calls
 * exist so the shared switch code can stay the same shape.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/armv7/context.hpp"
#include "kernel/arch/armv7/kernel_stack.hpp"

namespace cinux::arch {

/// Records the task's kernel stack top in the per-CPU block.
inline void ArmTaskStack(unsigned long long top) {
    SetKernelStackTop(static_cast<unsigned long>(top));
}

/// This port has no per-task address registers yet.
inline void StoreTaskAddressRegs(unsigned long /*fs_base*/, unsigned long /*gs_base*/) {}

/// The matching no-op.
inline void LoadTaskAddressRegs(unsigned long /*fs_base*/, unsigned long /*gs_base*/) {}

/// Single core: the outgoing task is off-CPU before the switch, so a
/// reclaim never waits for a core that will not come.
inline void SwitchTaskContext(CpuContext& from, const CpuContext& to, int* on_cpu) {
    if (on_cpu != nullptr) {
        *on_cpu = -1;
    }
    ContextSwitch(&from, &to);
}

}  // namespace cinux::arch
