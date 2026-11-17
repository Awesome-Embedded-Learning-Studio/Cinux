/**
 * @file    page_walk.hpp
 * @brief   The page-table grammar of the architecture this tree is built for.
 *
 * A forwarding include, for the same purpose as kernel/arch/space.hpp and
 * kernel/arch/page.hpp: consumers name the neutral path, and a port swaps
 * the include below for its own shape header.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/port/page_walk_shape.hpp"
