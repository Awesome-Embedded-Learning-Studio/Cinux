/**
 * @file    block_volume.hpp
 * @brief   The one ladder from a block device to a mounted ext2 volume.
 *
 * Both ports climb the same four steps: seat the device behind an erased
 * port, put one queue over it, cut a cache out of a shared memory budget,
 * and mount the ext2 volume the cache feeds. The steps are neutral; only
 * the device and the mount point are the port's words, so this face takes
 * those two and runs the order once.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_fs
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <string_view>

#include "cinux/result.hpp"
#include "kernel/fs/vfs.hpp"

namespace cinux::driver {
class BlockCache;
class BlockPort;
class BlockQueue;
}  // namespace cinux::driver

namespace cinux::fs {

namespace ext2 {
class Ext2;
}

/// The three objects the ladder builds, in the order it builds them.
/// The caller keeps them alive; the queue points back at the port, so the
/// port's own lifetime must cover every later call.
struct BlockVolume {
    driver::BlockQueue* queue  = nullptr;
    driver::BlockCache* cache  = nullptr;
    ext2::Ext2*         volume = nullptr;
    /// Whether the volume mounted and landed at the asked-for path.
    bool mounted = false;
};

/**
 * @brief         Builds the volume street over one block device and mounts
 *                it at a path.
 *
 * @param[in,out] vfs    The file system world taking the mount.
 * @param[in]     path   Where it lands; the parent must already exist.
 * @param[in,out] port   The device, already adopted into an erased port.
 * @param[in]     clock  Optional wall clock for the volume's own stamps.
 * @return        The three objects and whether the mount happened. A
 *                refused mount leaves the objects in place so the caller
 *                can still report or release them.
 * @note          The cache budget is the shared policy: a fraction of free
 *                physical memory, capped, cut into whole cache slots.
 * @since         0.1.0
 * @ingroup       kernel_fs
 */
BlockVolume MountBlockVolume(Vfs& vfs, std::string_view path, driver::BlockPort& port,
                             unsigned long long (*clock)() = nullptr);

}  // namespace cinux::fs
