/**
 * @file    task_switch.hpp
 * @brief   The port's part of entering a task: its stack, its root, its
 *          address registers.
 *
 * The shared switch code needs three things from the port before it can
 * hand the CPU to a task: where that task's kernel stack top is, which
 * root table it runs under, and whether it carries per-task address
 * registers. This header names those three without naming a CPU.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/port/task_switch.hpp"
