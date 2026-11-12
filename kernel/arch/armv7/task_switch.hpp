/**
 * @file    task_switch.hpp
 * @brief   A minimal two-task round robin over the context switch.
 *
 * This is not the scheduler; it is the smallest thing that proves the
 * pieces talk to each other: two stacks, two register images, and the
 * per-CPU block naming which one runs. The yield entry keeps the shape
 * of the scheduler's own seam, so replacing it later leaves the switch
 * and the per-CPU data untouched.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::armv7 {

/// Runs two hand-built tasks that yield to each other, then checks the counts.
[[nodiscard]] bool RunTaskSelfCheck();

}  // namespace cinux::armv7
