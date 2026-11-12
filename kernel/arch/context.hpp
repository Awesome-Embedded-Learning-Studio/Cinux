/**
 * @file    context.hpp
 * @brief   The task register image a switch saves and restores, published
 *          under a neutral path.
 *
 * Forwarding include: which registers a context carries, and how wide
 * they are, belongs to the port. The kernel's scheduler names
 * CpuContext; the fields behind it are the ISA's answer.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/x86_64/context.hpp"
