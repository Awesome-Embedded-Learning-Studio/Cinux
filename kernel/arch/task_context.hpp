/**
 * @file    task_context.hpp
 * @brief   Arming a kernel task's first context, port-provided.
 *
 * Forwarding include: what a task's registers look like at birth is the
 * port's business. The shared builder keeps the portable parts and calls
 * ArmTaskContext for the image itself.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/port/task_context.hpp"
