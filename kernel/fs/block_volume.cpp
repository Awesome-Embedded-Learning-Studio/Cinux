/**
 * @file    block_volume.cpp
 * @brief   The block-volume ladder, in one place.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_fs
 * @copyright Copyright (c) 2026
 */

#include "kernel/fs/block_volume.hpp"

#include "cinux/math.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/drivers/block/block_cache.hpp"
#include "kernel/drivers/block/block_config.hpp"
#include "kernel/drivers/block/block_device_concept.hpp"
#include "kernel/drivers/block/block_queue.hpp"
#include "kernel/fs/ext2/ext2.hpp"
#include "kernel/mm/pmm.hpp"

namespace cinux::fs {

BlockVolume MountBlockVolume(Vfs& vfs, std::string_view path, driver::BlockPort& port,
                             unsigned long long (*clock)()) {
    BlockVolume volume{};
    volume.queue = new driver::BlockQueue(port);
    const auto kAvailable = mm::Pmm::self().free_page_count() * arch::page::kSize;
    const auto kBudget =
        base::math::Min(kAvailable / driver::kBlockCacheMemoryDivisor, driver::kBlockCacheMaxBytes);
    const auto kSlotBytes = driver::kCachePageSectors * driver::kSectorBytes;
    volume.cache          = new driver::BlockCache(*volume.queue, kBudget / kSlotBytes);
    volume.volume         = new ext2::Ext2(*volume.cache, clock);
    if (!volume.volume->mount().ok()) {
        return volume;
    }
    volume.mounted = vfs.mount(path, *volume.volume, "ext2").ok();
    return volume;
}

}  // namespace cinux::fs
