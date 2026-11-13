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

#include "kernel/fs/file_world.hpp"
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

namespace {

unsigned long long wall_clock() {
    return driver::Rtc::self().now_seconds();
}

}  // namespace

void FileWorld::init() {
    queue_                = new driver::BlockQueue(driver::Ahci::self());
    const auto kAvailable = mm::Pmm::self().free_page_count() * arch::page::kSize;
    const auto kBudget =
        base::math::Min(kAvailable / driver::kBlockCacheMemoryDivisor, driver::kBlockCacheMaxBytes);
    const auto kSlotBytes = driver::kCachePageSectors * driver::kSectorBytes;
    cache_                = new driver::BlockCache(*queue_, kBudget / kSlotBytes);
    volume_               = new ext2::Ext2(*cache_, wall_clock);

    const base::Result<void> kMounted = volume_->mount();
    base::safety::Check(kMounted.ok(), "ext2 volume refused to mount");
    const base::Result<void> kRoot = vfs_.mount("/", *volume_, "ext2");
    base::safety::Check(kRoot.ok(), "ext2 root mount failed");

    // The tree grows through the neutral face; only the two input
    // sources and the console key tap are this port's words.
    driver::SetConsoleKeyTap(driver::ConsoleKeyTap{
        .take = +[](char* out) {
            const char kMaybe = driver::Keyboard::self().take(false);
            if (kMaybe != 0) {
                *out = kMaybe;
                return true;
            }
            if (driver::UsbKeyboard::self().poll()) {
                *out = driver::UsbKeyboard::self().take();
                return true;
            }
            return false;
        },
        .has = +[] {
            return driver::Keyboard::self().poll() || driver::UsbKeyboard::self().poll();
        },
    });
    [[maybe_unused]] const bool kUsbKeyboard = driver::SeatUsbKeyboard();
    const bool                  kTablet      = driver::UsbTablet::self().init();
    SeatDeviceTree(devfs_, driver::Keyboard::self().events(),
                   kTablet ? driver::UsbTablet::self().events()
                           : driver::Ps2Mouse::self().events());
    const base::Result<void> kSata = driver::DeviceModel::self().seat({
        .klass       = driver::DeviceClass::kBlock,
        .name        = "sata0",
        .number      = MakeDeviceId(8, 0),
        .ops         = nullptr,
        .bus         = "pci",
        .driver_name = "ahci",
    });
    base::safety::Check(kSata.ok(), "device model refused the sata0 seat");
    if (driver::Nvme::self().ready()) {
        const auto kNvme = driver::DeviceModel::self().seat({
            .klass       = driver::DeviceClass::kBlock,
            .name        = "nvme0",
            .number      = MakeDeviceId(259, 0),
            .ops         = nullptr,
            .bus         = "pci",
            .driver_name = "nvme",
        });
        base::safety::Check(kNvme.ok(), "device model refused the nvme0 seat");
    }
    const base::Result<void> kDevices = vfs_.mount("/dev", devfs_, "devtmpfs");
    base::safety::Check(kDevices.ok(), "devfs mount failed");

    WireKernelProcSource(procfs_);
    const base::Result<void> kFacts = vfs_.mount("/proc", procfs_, "proc");
    base::safety::Check(kFacts.ok(), "procfs mount failed");
}

base::Result<FsView> FileWorld::resolve(std::string_view path) {
    return vfs_.resolve(path);
}

Vfs& FileWorld::vfs() {
    return vfs_;
}

Vfs& kernel_vfs() {
    return FileWorld::self().vfs();
}

base::Result<void> FileWorld::seat_console_streams(FileTable& table) {
    const base::Result<FsView> kView = vfs_.resolve("/dev/console");
    if (!kView.ok()) {
        return kView.error();
    }
    const FsView&              flat  = kView.value();
    const base::Result<FsNode> kNode = flat.ops->lookup(flat.self, "console");
    if (!kNode.ok()) {
        return kNode.error();
    }
    flat.ops->retain(flat.self, kNode.value());
    const base::Shared<OpenFile> kStream{new OpenFile(flat.ops, flat.self, kNode.value())};
    if (!kStream.holds()) {
        flat.ops->release(flat.self, kNode.value());
        return base::KernelError::kOutOfMemory;
    }
    kStream->status_flags = kFsReadWriteStatus;
    for (unsigned long seat = 0; seat < 3; ++seat) {
        const base::Result<unsigned long> kSeated = table.adopt_at(kStream, seat);
        if (!kSeated.ok()) {
            return kSeated.error();
        }
    }
    return {};
}

unsigned long FileWorld::mounts_text(char* out, unsigned long capacity) const {
    return vfs_.mounts_text(out, capacity);
}

}  // namespace cinux::fs
