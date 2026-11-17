/**
 * @file    tmpfs_mount.hpp
 * @brief   The mount syscall's tmpfs faces: port-neutral volume seats.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_fs
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <string_view>

#include "kernel/fs/fs_config.hpp"
#include "cinux/result.hpp"

namespace cinux::fs {

/**
 * @brief         Seats a fresh tmpfs volume at an existing directory.
 * @param[in]     target  The mount point path.
 * @return        Success, or the mount table's refusal.
 * @since         0.1.0
 * @ingroup       kernel_fs
 */
[[nodiscard]] base::Result<void> mount_tmpfs(std::string_view target);

/**
 * @brief         Takes one tmpfs seat back out of the mount table.
 * @param[in]     target  The mount point as it was mounted.
 * @param[in]     lazy    Skip the busy check, drop the seat anyway.
 * @return        Success, kWouldBlock when live leases refuse, or
 *                kNotFound when no seat carries that prefix.
 * @since         0.1.0
 * @ingroup       kernel_fs
 */
[[nodiscard]] base::Result<void> umount_tmpfs(std::string_view target, bool lazy);

}  // namespace cinux::fs
