/**
 * @file    io.hpp
 * @brief   The io facilities of the machine, published under a
 *          platform path so drivers never name an ISA directory.
 *
 * Forwarding include: what backs this facility is the port's business.
 * A machine without it answers absent and the driver refuses to start,
 * which is the contract every consumer already checks.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_platform
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/platform/io.hpp"
