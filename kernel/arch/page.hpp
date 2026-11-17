/**
 * @file    page.hpp
 * @brief   The page size of the architecture this tree is built for.
 *
 * The forwarding include names the current port; consumers write
 * "kernel/arch/page.hpp" and stay free of ISA directories. A port adds
 * its own page header and repoints the include below.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/port/page.hpp"
