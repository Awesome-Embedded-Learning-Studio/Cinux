/**
 * @file    per_cpu.hpp
 * @brief   The per-CPU block and its accessor, published under a neutral
 *          path.
 *
 * Forwarding include: how a processor finds its own block (a segment
 * base, a dedicated register, a table keyed by CPU id) is the port's
 * answer, so the neutral layer names the block and the accessor only.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/port/per_cpu.hpp"
