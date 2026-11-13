/**
 * @file    user_fs.hpp
 * @brief   The armv7 user world's file system: one ramfs volume, one
 *          console face, both spoken to through the shared Vfs layer.
 *
 * This is the replacement for the per-descriptor stand-in table: the
 * same kernel/fs code the x86 kernel runs, seeded with the bring-up
 * lane's built-in files. Nothing here re-implements a mechanism.
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
class RamFs;
class Vfs;
}  // namespace cinux::fs

namespace cinux::mm {
class VmaStore;
class AddressSpace;
}  // namespace cinux::mm

namespace cinux::armv7::userfs {

/// Seeds the volume and mounts it: /hello, /motd, /readme, /scratch.
/// Call once from kernel init before the first task runs.
void init();

/// The mount table the user world resolves through.
cinux::fs::Vfs& vfs();

/// The one backend volume behind the root mount.
cinux::fs::RamFs& ram();

/// The erased face of the UART console behind descriptors 0-2.

/// The scratch file's byte ceiling; writes past it answer ENOSPC,
/// matching the stand-in's fixed window.
inline constexpr unsigned long kScratchRoom = 64;

/// The loaded guest's world, or null when no image seated: the neutral
/// loader's output, waiting for the user-mode lane to run it.
cinux::mm::AddressSpace* take_guest();

/// The loader-written promise register for the guest's segments; the
/// seated task adopts it so the neutral demand pager fills its pages.
cinux::mm::VmaStore* take_guest_ranges();

/// Where the loaded guest's first instruction lives.
unsigned long guest_entry();

/// Marks the guest as owned by its task; the loader's copy retires.

}  // namespace cinux::armv7::userfs
