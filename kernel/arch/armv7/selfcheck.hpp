/**
 * @file    selfcheck.hpp
 * @brief   In-image capability checks for the armv7 lane.
 *
 * These are small, load-bearing assertions about kernel services, run
 * inside the image itself: no host framework, no x86 boot chain. The
 * first one covers the physical page manager.
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

/// Allocates, touches, frees, and refuses an impossible order.
[[nodiscard]] bool RunPmmSelfCheck();

/// Exercises the heap allocator over a static arena.
[[nodiscard]] bool RunHeapSelfCheck();

/// Maps one fresh page through the shared walk and writes through it.
[[nodiscard]] bool RunMapSelfCheck();

/// Switches into a hand-built context and checks it switched back.
[[nodiscard]] bool RunContextSelfCheck();

/// Installs a per-CPU block and reads it back through TPIDRPRW.
[[nodiscard]] bool RunPerCpuSelfCheck();

/// Runs two tasks that yield to each other and checks the step counts.
[[nodiscard]] bool RunTaskSelfCheck();

/// Drops to user mode, makes one svc, and comes back through a second.
[[nodiscard]] bool RunUserModeSelfCheck();

}  // namespace cinux::armv7
