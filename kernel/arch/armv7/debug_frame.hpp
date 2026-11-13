/**
 * @file    debug_frame.hpp
 * @brief   The armv7 frame pointer, for a panic dump to walk.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::arch {

/**
 * @brief         Reads the current frame pointer.
 * @return        R11 as the dump may walk it.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long long FramePointer() {
    unsigned long long frame = 0;  // NOLINT(misc-const-correctness) written by the asm output
    __asm__ volatile("mov %0, r11" : "=r"(frame));
    return frame;
}

}  // namespace cinux::arch
