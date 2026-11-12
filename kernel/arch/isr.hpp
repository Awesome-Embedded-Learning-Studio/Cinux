/**
 * @file    isr.hpp
 * @brief   The exception and fault vocabulary, published under a neutral
 *          path.
 *
 * Forwarding include: vector numbers and fault-code bit meanings are
 * hardware facts and live with the port. The memory subsystem asks for
 * the page-fault vector and the protection/write/user bits by name; what
 * the port fills in is its own business. On ARMv7, for instance, the
 * page fault arrives as a data abort with its own status register, and
 * the port maps those bits onto the same names.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/x86_64/isr.hpp"
