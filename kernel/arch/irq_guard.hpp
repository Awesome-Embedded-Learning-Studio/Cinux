/**
 * @file    irq_guard.hpp
 * @brief   The interrupt-flag discipline of the architecture this tree is
 *          built for, published under a neutral path.
 *
 * Forwarding include, same contract as the other neutral arch headers: a
 * port repoints the line below at its own critical-section primitives.
 * The host-side stand-in keeps living in the test framework, not here.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/port/irq_guard.hpp"
