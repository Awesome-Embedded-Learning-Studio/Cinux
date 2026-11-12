/**
 * @file    user_trap.hpp
 * @brief   The full user trap frame: every register an interrupt may land on.
 *
 * A supervisor call only needs r4-r11 and SPSR to resume, because r0-r3
 * are its arguments and answer by convention. An interrupt promises
 * nothing: it may land mid-expression, so r0-r12, sp, lr and SPSR all
 * travel together. The offsets are what vectors.S and user_entry.S write.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::arch {

struct UserTrap {
    unsigned long r0;
    unsigned long r1;
    unsigned long r2;
    unsigned long r3;
    unsigned long r4;
    unsigned long r5;
    unsigned long r6;
    unsigned long r7;
    unsigned long r8;
    unsigned long r9;
    unsigned long r10;
    unsigned long r11;
    unsigned long r12;
    unsigned long sp;
    unsigned long lr;
    unsigned long spsr;
};

static_assert(sizeof(UserTrap) == 16 * sizeof(unsigned long), "trap is sixteen words");

}  // namespace cinux::arch
