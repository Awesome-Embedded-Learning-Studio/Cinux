/**
 * @file    waiter.hpp
 * @brief   The one turn a blocking handler takes while it waits.
 *
 * Every waiting handler in the syscall face does the same three things in
 * the same order: let the port drain its input, let interrupts run so the
 * timer can fill the clock the wait is measured against, and let the
 * scheduler give the CPU to someone else. Written by hand in each loop the
 * order drifts -- nanosleep had lost the interrupt half and waited on a
 * clock no tick advanced -- so it lives here once.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.2.0
 * @since   0.1.0
 * @ingroup kernel_proc
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::proc {

/**
 * @brief         Takes one waiting turn: pump, open interrupts, yield.
 *
 * @note          Call it inside a wait loop, once per iteration, after the
 *                loop has found its condition unsatisfied. The interrupt
 *                gate is restored to whatever it was on entry, so a caller
 *                that already runs with interrupts masked keeps them that
 *                way outside the turn.
 * @since         0.1.0
 * @ingroup       kernel_proc
 */
void WaitForNextTurn();

}  // namespace cinux::proc
