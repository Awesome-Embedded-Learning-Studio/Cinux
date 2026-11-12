/**
 * @file    halt.hpp
 * @brief   The armv7 parking instruction.
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

/**
 * @brief         Parks the CPU forever with interrupts as they stand.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
[[noreturn]] inline void Halt() {
    for (;;) {
        asm volatile("wfi");
    }
}

/**
 * @brief         Opens the interrupt gate and parks, as one adjacent pair.
 * @note          On armv7 the wake is not atomic with the enable; the
 *                caller re-checks its condition after every wake.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
[[noreturn]] inline void EnableIrqAndHalt() {
    for (;;) {
        asm volatile("cpsie i" ::: "memory");
        asm volatile("wfi");
    }
}

}  // namespace cinux::arch
