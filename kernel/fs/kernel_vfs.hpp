/**
 * @file    kernel_vfs.hpp
 * @brief   The one way in for any kernel code asking the mounted world.
 *
 * Each port owns exactly one running Vfs — x86-64 seats it inside the
 * FileWorld singleton, armv7 inside its userfs lane — but the asker
 * must not know which. This face hands back whichever mount table this
 * kernel booted with, so the loader, the exec path and friends stay
 * written once. One mechanism, one implementation per port.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_fs
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/fs/vfs.hpp"

namespace cinux::fs {

/**
 * @brief   Returns the one mount table this kernel booted with.
 * @note    Defined once per port next to its own Vfs instance; calls
 *          before the port's mount wiring have no contract.
 * @since   0.1.0
 * @ingroup kernel_fs
 */
[[nodiscard]] Vfs& kernel_vfs();

}  // namespace cinux::fs
