/**
 * @file    user_fs.hpp
 * @brief   The armv7 user world's file system: the card's volume and a
 *          console face, both spoken to through the shared Vfs layer.
 *
 * This is the replacement for the per-descriptor stand-in table: the
 * same kernel/fs code the x86 kernel runs, mounted over the card the
 * boot was given. Nothing here re-implements a mechanism.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::fs {
struct FsOps;
class Vfs;
}  // namespace cinux::fs

namespace cinux::armv7::userfs {

/// Mounts the card's volume and seats the console face. Call once from
/// kernel init before the first task runs.
void init();

/// The mount table the user world resolves through.
cinux::fs::Vfs& vfs();

}  // namespace cinux::armv7::userfs
