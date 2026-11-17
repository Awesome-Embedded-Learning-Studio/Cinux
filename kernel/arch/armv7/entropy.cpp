/**
 * @file    entropy.cpp
 * @brief   Seeding the port's random pool from the generic timer.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/entropy.hpp"

extern char _start[];

namespace cinux::arch {

namespace {

/// The generic timer's free-running counter; it counts at a rate the
/// board sets and no two boots read the same low bits.
unsigned long long counter_count() {
    unsigned int  low  = 0;
    unsigned int  high = 0;
    asm volatile("mrrc p15, 0, %0, %1, c14" : "=r"(low), "=r"(high));
    return (static_cast<unsigned long long>(high) << 32U) | static_cast<unsigned long long>(low);
}

}  // namespace

void SeedKernelRandom() {
    auto seed = reinterpret_cast<unsigned long long>(_start);
    seed ^= counter_count();
    seed ^= counter_count() << 17U;
    base::random::Seed(g_random_generator, seed);
}

unsigned long long RandomWord() {
    return base::random::Next(g_random_generator);
}

}  // namespace cinux::arch
