/**
 * @file    user_seat.hpp
 * @brief   The seated user-process faces: one probe, one file-table
 *          bridge, one return service.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/fs/file_table.hpp"

namespace cinux::armv7 {

/// A real scheduler task's user process: built, seated on its own
/// stack, one console write from user mode, retired clean.
[[nodiscard]] bool RunSchedulerUserSelfCheck();

/// The seated process's open files, or an empty hold; the console
/// faces consult it before the bring-up round-robin's table.
[[nodiscard]] cinux::base::Shared<cinux::fs::FileTable> seated_files();

/// The return service's landing: parks the user frame and switches
/// back to the waiting kernel body. Does not return.
[[noreturn]] void SeatReturnToKernel();

/// True while a seated process's service call may arrive: the svc
/// table asks before touching the bring-up round-robin.
[[nodiscard]] bool SeatWorldActive();

/// The exit service for a seated process: records the exit word and
/// parks the user frame back into its waiting body. Does not return.
[[noreturn]] void SeatExit(unsigned long status);

}  // namespace cinux::armv7
