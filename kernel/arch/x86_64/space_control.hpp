/**
 * @file    space_control.hpp
 * @brief   The x86-64 controls over the live address space: which root
 *          table the CPU walks, and how one translation is dropped.
 *
 * The memory subsystem changes page tables, but only the port knows how
 * to tell the CPU about it. Four actions cover every caller: read the
 * active root, switch to another root, refill the active root (which
 * flushes everything), and invalidate one page after a leaf changed.
 * Raw CR access stays below this line; callers name the action, not the
 * register.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/x86_64/registers.hpp"

namespace cinux::arch::space {

/**
 * @brief         Reports the physical address of the root table in use.
 * @return        CR3's frame, low flag bits included.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long ActiveRoot() {
    return ReadCr3();
}

/**
 * @brief         Points the CPU at another root table.
 * @param[in]     physical   Physical address of the new root.
 * @note          Flushes every non-global translation on this CPU.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline void SwitchRoot(unsigned long physical) {
    LoadCr3(physical);
}

/**
 * @brief         Reloads the active root, flushing the whole TLB.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline void RefreshRoot() {
    ReloadCr3();
}

/**
 * @brief         Drops one translation from this CPU's TLB.
 * @param[in]     virtual_address   The page whose leaf just changed.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline void InvalidatePage(unsigned long virtual_address) {
    cinux::arch::InvalidatePage(virtual_address);
}

/**
 * @brief         Reads the faulting address the last page fault named.
 * @return        CR2 as the CPU left it.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long FaultAddress() {
    return ReadCr2();
}

}  // namespace cinux::arch::space
