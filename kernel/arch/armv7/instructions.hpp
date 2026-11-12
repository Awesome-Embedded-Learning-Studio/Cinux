/**
 * @file    instructions.hpp
 * @brief   The armv7 instruction-level primitives the core may name.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::arch {

/// Lets the core breathe while another waits for it.
inline void CpuRelax() {
    asm volatile("yield");
}

/// Full memory barrier.
inline void MemoryBarrier() {
    asm volatile("dsb" ::: "memory");
}

/// Instruction synchronization barrier.
inline void InstructionBarrier() {
    asm volatile("isb" ::: "memory");
}

}  // namespace cinux::arch
