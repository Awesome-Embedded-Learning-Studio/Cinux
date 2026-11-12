/**
 * @file    task_switch.hpp
 * @brief   x86-64's part of entering a task.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/x86_64/kernel_stack.hpp"
#include "kernel/arch/x86_64/per_cpu.hpp"

namespace cinux::arch {

/// The TSS stack and the per-CPU copy both follow the task's stack top.
inline void ArmTaskStack(unsigned long long top) {
    SetKernelStackTop(static_cast<unsigned long>(top));
    per_cpu::PerCpu::self().kernel_rsp = top;
}

/// Saves the outgoing task's address-register bases into the CPU's slots.
inline void StoreTaskAddressRegs(unsigned long long& fs_base, unsigned long long& gs_base) {
    per_cpu::StoreTaskAddressRegs(fs_base, gs_base);
}

/// Loads the incoming task's address-register bases from its context.
inline void LoadTaskAddressRegs(unsigned long long fs_base, unsigned long long gs_base) {
    per_cpu::LoadTaskAddressRegs(fs_base, gs_base);
}

}  // namespace cinux::arch
