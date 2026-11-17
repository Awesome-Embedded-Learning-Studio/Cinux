/**
 * @file    entropy.hpp
 * @brief   The kernel's random pool: one xorshift generator, seeded at
 *          boot from whatever entropy this port can honestly reach.
 *
 * The board has no hardware RNG and QEMU models none, so the seed
 * mixes the generic timer's free-running counter with a link-time
 * address. That is honest bring-up entropy — enough to decorrelate
 * stack slides and cookie draws, not a cryptographic source, and the
 * contract below says so.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "cinux/random.hpp"

namespace cinux::arch {

/// The port's whole pool; one generator, drawn through RandomWord().
inline base::random::Generator g_random_generator{};

/// Seeds the pool from the timer counter and a link-time address.
void SeedKernelRandom();

/// Draws one word from the seeded pool.
unsigned long long RandomWord();

}  // namespace cinux::arch
