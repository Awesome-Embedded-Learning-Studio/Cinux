/**
 * @file    syscall_numbers.hpp
 * @brief   The armv7 syscall numbers this lane serves, spelled as Linux does.
 *
 * Keeping the numbers identical to the arm EABI means a program compiled
 * for Linux calls them the same way; what differs is how much of each
 * call is implemented.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::armv7::syscall_nr {

inline constexpr unsigned long kExit        = 1;
inline constexpr unsigned long kRead        = 3;
inline constexpr unsigned long kWrite       = 4;
inline constexpr unsigned long kSchedYield  = 158;
inline constexpr unsigned long kWait        = 114;
inline constexpr unsigned long kSleep       = 162;
inline constexpr unsigned long kGetPid      = 20;
inline constexpr unsigned long kBrk         = 45;

}  // namespace cinux::armv7::syscall_nr
