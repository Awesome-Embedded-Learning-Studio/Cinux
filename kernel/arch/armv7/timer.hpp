/**
 * @file    timer.hpp
 * @brief   The ARM generic timer: one counter, one frequency, one compare.
 *
 * CNTFRQ names the counter's rate; CNTPCT counts up from zero at that rate;
 * CNTP_TVAL arms a compare that fires the physical timer's interrupt when
 * it reaches zero. On a port with a different timekeeper these three
 * become that port's answer to platform/clock.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

namespace cinux::armv7 {

/// Reads CNTFRQ: the counter's ticks per second.
[[nodiscard]] std::uint32_t TimerFrequency();

/// Reads CNTPCT: the physical counter, 64 bits wide.
[[nodiscard]] std::uint64_t TimerCounter();

/// Arms the physical timer to fire after the given ticks.
void TimerArmAfter(std::uint32_t ticks);

/// Opens or closes the tick heartbeat trace; the station world
/// listens from boot, the init lane closes it at its door.
void SetTickTrace(bool on);

/// Enables the physical timer's compare and interrupt generation.
void TimerEnable();

}  // namespace cinux::armv7
