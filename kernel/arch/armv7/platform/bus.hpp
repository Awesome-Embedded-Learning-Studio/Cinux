/**
 * @file    bus.hpp
 * @brief   This board has no enumerable bus; the face is deliberately empty.
 *
 * The i.MX6ULL brings its devices up from the device tree, so there is no
 * PCI-like enumeration to offer. Consumers already treat a missing
 * facility as absent and refuse to start; an empty aggregate says the
 * same thing without pretending a bus exists.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_platform
 * @copyright Copyright (c) 2026
 */

#pragma once
