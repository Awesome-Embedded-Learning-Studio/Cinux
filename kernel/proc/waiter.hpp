/**
 * @file    waiter.hpp
 * @brief   The neutral word a blocking handler says before it waits.
 *
 * An exception entry masks interrupts on both ports -- the CPU does that,
 * not the kernel. A handler that is about to wait (nanosleep, poll, wait)
 * must let them back in, or the clock it is waiting on never advances.
 * The words differ (armv7's cpsie i, x86-64's sti, a port that already
 * runs syscalls with interrupts live says nothing), so each port answers
 * this one call; the neutral handlers stay free of assembly.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_proc
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::proc {

/**
 * @brief         Lets interrupts run while the caller waits.
 *
 * @note          Call it once, right before a wait loop that yields. It is
 *                idempotent and stays on until the handler returns to user
 *                mode: the port's return path restores the user's own
 *                interrupt state.
 * @since         0.1.0
 * @ingroup       kernel_proc
 */
void AllowInterruptsWhileWaiting();

}  // namespace cinux::proc
