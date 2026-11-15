/**
 * @file    waiter.cpp
 * @brief   x86-64's answer to the blocking handler's request.
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
    // This port's service-call entry already leaves the interrupt flag on:
    // its wait loops have always seen the timer. The word is a no-op here
    // rather than a second gate to keep in step.
}

}  // namespace cinux::proc
