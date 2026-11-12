/**
 * @file    interrupt.hpp
 * @brief   x86-64's interrupt facilities, gathered for the neutral face.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_platform
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/x86_64/platform/apic/local_apic.hpp"
#include "kernel/arch/x86_64/platform/msi/msi_layout.hpp"
#include "kernel/arch/x86_64/platform/msi/msix.hpp"
