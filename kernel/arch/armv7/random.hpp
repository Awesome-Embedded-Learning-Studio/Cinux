/**
 * @file    random.hpp
 * @brief   The kernel's random face for armv7: one seeded pool, no
 *          hardware source on this board.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/armv7/entropy.hpp"

namespace cinux::arch::random {

/**
 * @brief         Draws one word from the kernel's random source.
 * @return        A word; the pool answers even though this board has
 *                no hardware RNG — bring-up entropy, not crypto.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long long Word() {
    return cinux::arch::RandomWord();
}

/**
 * @brief         Tries one word from a hardware source.
 * @param[out]    out   Would receive the word.
 * @return        Always false: the board exposes no hardware RNG.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline bool TryWord(unsigned long long& out) {
    static_cast<void>(out);
    return false;
}

}  // namespace cinux::arch::random
