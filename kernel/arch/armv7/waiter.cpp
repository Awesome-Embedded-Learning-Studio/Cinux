/**
 * @file    waiter.cpp
 * @brief   armv7's answer to the blocking handler's request.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/proc/waiter.hpp"

namespace cinux::proc {

void AllowInterruptsWhileWaiting() {
    // The SVC exception masked IRQ on entry; opening the gate here lets the
    // timer tick fill the clock the wait is measured against. The return
    // path restores the user's own SPSR, so this never leaks into user mode.
    asm volatile("cpsie i" ::: "memory");
}

}  // namespace cinux::proc
