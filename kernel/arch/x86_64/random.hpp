/**
 * @file    random.hpp
 * @brief   The x86-64 random source: RDRAND where present, the entropy
 *          pool otherwise.
 *
 * Two actions: draw a word (the pool always answers, mixing hardware and
 * soft entropy) and try a word (false when no hardware source answered).
 * A port fills both from whatever its machine offers: an SoC RNG, a
 * jitter counter, or nothing.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/x86_64/entropy.hpp"
#include "kernel/arch/x86_64/rdrand.hpp"

namespace cinux::arch::random {

/**
 * @brief         Draws one word from the kernel's random source.
 * @return        A word; never fails, the pool answers even without a
 *                hardware RNG.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline unsigned long long Word() {
    return cinux::arch::RandomWord();
}

/**
 * @brief         Tries one word from the hardware source alone.
 * @param[out]    out   Receives the word when the hardware answered.
 * @return        true when a hardware draw happened.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline bool TryWord(unsigned long long& out) {
    return cinux::arch::TryRdrand(out);
}

}  // namespace cinux::arch::random
