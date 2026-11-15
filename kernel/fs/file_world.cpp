/**
 * @file    file_world.cpp
 * @brief   Wiring the world: one queue, one volume, one root mount.
 *
 * @author  Charliechen114514
 * @date    2026-10-11
 * @version 0.3
 * @since   0.1.0
 * @ingroup kernel_fs
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/console_world.hpp"
#include "kernel/fs/file_world.hpp"
#include "kernel/drivers/block/block_device_concept.hpp"
#include "kernel/fs/block_volume.hpp"
#include "kernel/fs/kernel_vfs.hpp"

#include <string_view>

#include "cinux/assert.hpp"
#include "cinux/math.hpp"
#include "cinux/memory.hpp"
#include "cinux/result.hpp"
#include "cinux/shared.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/drivers/ahci/ahci.hpp"
#include "kernel/drivers/block/block_cache.hpp"
#include "kernel/drivers/block/block_config.hpp"
#include "kernel/drivers/block/block_queue.hpp"
#include "kernel/drivers/device/device.hpp"
#include "kernel/drivers/keyboard/keyboard.hpp"
#include "kernel/drivers/mouse/ps2_mouse.hpp"
#include "kernel/drivers/nvme/nvme.hpp"
#include "kernel/drivers/rtc/rtc.hpp"
#include "kernel/drivers/usb/usb_keyboard.hpp"
#include "kernel/drivers/usb/usb_tablet.hpp"
#include "kernel/fs/devfs/console_node.hpp"
#include "kernel/drivers/keyboard/keyboard.hpp"
#include "kernel/drivers/tty/console_tty.hpp"
#include "kernel/drivers/usb/usb_keyboard.hpp"
#include "kernel/fs/devfs/devfs.hpp"
#include "kernel/fs/device_tree.hpp"
#include "kernel/fs/devfs/fb_node.hpp"
#include "kernel/fs/devfs/input_node.hpp"
#include "kernel/fs/devfs/pty_node.hpp"
#include "kernel/fs/ext2/ext2.hpp"
#include "kernel/fs/file_table.hpp"
#include "kernel/fs/fs_config.hpp"
#include "kernel/fs/mount_lease.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/fs/procfs/procfs_init.hpp"
#include "kernel/fs/ramfs/ramfs.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/mm/pmm.hpp"

namespace cinux::fs {

fs::DevFs& FileWorld::devices() {
    return devfs_;
}

void FileWorld::adopt(Vfs& borrowed_vfs) {
    borrowed_ = &borrowed_vfs;
}

Vfs& FileWorld::world() const {
    if (borrowed_ != nullptr) {
        return *borrowed_;
    }
    return const_cast<FileWorld*>(this)->vfs_;
}

void FileWorld::init(driver::BlockPort& root_port, unsigned long long (*wall_clock)()) {
    // The neutral ladder: erased port, queue, cache, volume, mount. Only
    // the device and the mount point are this port's words.
    // The port naming the root volume is the caller's word: the x86
    // lane hands one adopted over its SATA host, another port could
    // hand the very card a bring-up lane mounted. The ladder from the
    // port up -- queue, cache, volume, the "/" seat -- is the shared
    // one either way.
    block_port_ = &root_port;
    const fs::BlockVolume kVolume = fs::MountBlockVolume(vfs_, "/", *block_port_, wall_clock);
    queue_                        = kVolume.queue;
    cache_                        = kVolume.cache;
    volume_                       = kVolume.volume;
    base::safety::Check(kVolume.mounted, "ext2 root mount failed");

    // The console world -- key taps, input event seats, the disk model
    // rows -- is the port's own word and grows through the arch hook
    // right after the mounts; the tree itself is shared.
    arch::SeatConsoleWorld(devfs_);
    const base::Result<void> kDevices = vfs_.mount("/dev", devfs_, "devtmpfs");
    base::safety::Check(kDevices.ok(), "devfs mount failed");

    WireKernelProcSource(procfs_);
    const base::Result<void> kFacts = vfs_.mount("/proc", procfs_, "proc");
    base::safety::Check(kFacts.ok(), "procfs mount failed");
}

base::Result<FsView> FileWorld::resolve(std::string_view path) {
    return world().resolve(path);
}

Vfs& FileWorld::vfs() {
    return world();
}

Vfs& kernel_vfs() {
    return FileWorld::self().vfs();
}

base::Result<void> FileWorld::seat_console_streams(FileTable& table) {
    return SeatConsoleStreams(world(), table);
}

unsigned long FileWorld::mounts_text(char* out, unsigned long capacity) const {
    return world().mounts_text(out, capacity);
}

}  // namespace cinux::fs
