/**
 * @file    user_fs.cpp
 * @brief   Wiring the armv7 user world onto the shared fs stack.
 *
 * The card's ext2 volume is the one file world, and the UART console
 * appears as a character-device face so descriptors 0-2 can live in the
 * same FileTable as everything else.
 * The console's read keeps the stand-in's line discipline: drain while
 * bytes are ready, stop after a newline. Park-until-input stays in the
 * syscall layer, which knows about tasks.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/abi.hpp"

#include <string_view>
#include "kernel/arch/armv7/user_fs.hpp"

#include "cinux/result.hpp"
#include "kernel/arch/armv7/errno.hpp"
#include "kernel/arch/armv7/user_access.hpp"
#include "kernel/arch/armv7/uart.hpp"
#include "kernel/fs/file_world.hpp"
#include "kernel/fs/fs_config.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/fs/kernel_vfs.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/drivers/usb/usb_keyboard.hpp"
#include "kernel/drivers/usb/usb_tablet.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/fs/file_table.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/arch/armv7/lcdif.hpp"
#include "kernel/arch/console_world.hpp"
#include "kernel/arch/root_port.hpp"
#include "kernel/arch/armv7/uart_input.hpp"
#include "kernel/fs/procfs/procfs.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/fs/devfs/devfs.hpp"
#include "kernel/fs/device_tree.hpp"
#include "kernel/fs/devfs/fb_node.hpp"
#include "kernel/fs/devfs/input_node.hpp"
#include "kernel/drivers/input/event_stream.hpp"
#include "kernel/drivers/input/input_config.hpp"
#include "kernel/drivers/block/block_cache.hpp"
#include "kernel/drivers/block/block_config.hpp"
#include "kernel/drivers/block/block_device_concept.hpp"
#include "kernel/drivers/block/block_queue.hpp"
#include "kernel/drivers/device/device.hpp"
#include "kernel/drivers/usdhc/usdhc.hpp"
#include "kernel/fs/ext2/ext2.hpp"
#include "kernel/fs/block_volume.hpp"
#include "kernel/drivers/framebuffer/framebuffer.hpp"

namespace cinux::armv7::userfs {

void init() {
    static bool seeded = false;
    if (seeded) {
        return;
    }
    seeded = true;

    // The screen joins the fs: the LCDIF's frame, adopted by the
    // neutral Framebuffer singleton, seats fb0 inside a real devfs.
    const unsigned long kFrame = lcdif::InitFramebuffer();
    if (kFrame != 0) {
        const boot::FramebufferInfo kInfo{
            .physical   = kFrame,
            .pitch      = static_cast<std::uint32_t>(lcdif::kWidth * 4),
            .width      = static_cast<std::uint32_t>(lcdif::kWidth),
            .height     = static_cast<std::uint32_t>(lcdif::kHeight),
            .bpp        = 32,
            .red_size   = 8,
            .red_shift  = 16,
            .green_size = 8,
            .green_shift = 8,
            .blue_size  = 8,
            .blue_shift = 0,
        };
        base::safety::Check(driver::Framebuffer::self().init(kInfo),
                            "framebuffer refused the LCDIF geometry");
        // The card's own facts -- the sector count and the partition
        // table -- are read through the erased port, facts only.
        if (driver::UsdhcHost::self().block_count() != 0) {
            driver::BlockPort port;
            port.adopt(driver::UsdhcHost::self());
            static unsigned char kProbe[512] = {};
            const bool kRead = port.read_blocks(driver::Lba{0},
                                                driver::BlockSpan{1}, kProbe);
            cinux::print::Println(kRead && kProbe[510] == 0x55 && kProbe[511] == 0xAA
                                      ? "[armv7] dev mmcblk0 %llu sectors mbr 55AA"
                                      : "[armv7] dev mmcblk0 read FAILED",
                                  port.block_count());
            // The partition table rides in the same MBR the signature
            // guards: the first entry names one Linux partition, and
            // its first sector answers through the very same port.
            if (kRead && kProbe[510] == 0x55 && kProbe[511] == 0xAA) {
                const std::uint32_t kStart = static_cast<std::uint32_t>(kProbe[454]) |
                                             (static_cast<std::uint32_t>(kProbe[455]) << 8U) |
                                             (static_cast<std::uint32_t>(kProbe[456]) << 16U) |
                                             (static_cast<std::uint32_t>(kProbe[457]) << 24U);
                static unsigned char kFirst[512] = {};
                const bool kEntry = kProbe[450] == 0x83 && kStart != 0 &&
                                    port.read_blocks(driver::Lba{kStart},
                                                     driver::BlockSpan{1}, kFirst);
                cinux::print::Println(kEntry ? "[armv7] mmcblk0 p1 at %lu"
                                             : "[armv7] mmcblk0 p1 missing",
                                      static_cast<unsigned long>(kStart));
                if (kEntry) {
                    // The one door every lane mounts through: the port
                    // names this card's own partition, and the neutral
                    // ladder behind it -- queue, cache, volume, the "/"
                    // seat, the console world, devfs and procfs -- is
                    // the very one the x86 lane climbs. The first
                    // reader through the queue drives it synchronously,
                    // so mounting works before the scheduler runs.
                    unsigned long long (*wall_clock)() = nullptr;
                    driver::BlockPort* const kRootPort = arch::MakeRootPort(&wall_clock);
                    fs::FileWorld::self().init(*kRootPort, wall_clock);
                    fs::Vfs& kWorld = fs::FileWorld::self().vfs();
                    fs::DevFs& kDev = fs::FileWorld::self().devices();
                    if (driver::SeatUsbKeyboard()) {
                        cinux::print::Println("[armv7] dev usbkbd seated");
                    }
                    const auto kMotd = kWorld.resolve("/etc/motd");
                    base::Own<unsigned char[]> kMotdBytes;
                    unsigned long              kMotdSize = 0;
                    const bool kMotdRead = kMotd.ok() &&
                                           fs::ReadWholeFile(kMotd.value(), kMotdBytes,
                                                             kMotdSize)
                                               .ok() &&
                                           kMotdSize != 0;
                    cinux::print::Println(
                        kMotdRead ? "[armv7] ext2 mounted, motd is %lu bytes"
                                  : "[armv7] ext2 mounted, motd read FAILED",
                        kMotdSize);
                    // The shell needs no copy any more: /bin/sh on the
                    // card is a fast symlink over busybox, and the
                    // card's own ext2 walk expands it.
                    const auto kShellView = kWorld.resolve("/bin/sh");
                    cinux::print::Println(kShellView.ok() ? "[armv7] bin/sh seated from the card"
                                                          : "[armv7] bin/sh missing on the card");
                    // Read-back probe: /proc/cpuinfo must answer through
                    // the very vfs the syscalls use, and the first record
                    // carries the port's vendor.
                    const auto kView = kWorld.resolve("/proc/cpuinfo");
                    base::safety::Check(kView.ok(), "proc cpuinfo resolve failed");
                    const auto kNode =
                        kView.value().ops->lookup(kView.value().self, kView.value().rest);
                    base::safety::Check(kNode.ok(), "proc cpuinfo lookup failed");
                    char         record[96]{};
                    const auto   kProcRead = kView.value().ops->read(kView.value().self,
                                                                     kNode.value(), 0, record,
                                                                     sizeof(record));
                    base::safety::Check(kProcRead.ok() && kProcRead.value() != 0,
                                        "proc cpuinfo read failed");
                    const unsigned long kBytes    = kProcRead.value();
                    bool                saw_vendor = false;
                    for (unsigned long index = 0; index + 4 < kBytes; ++index) {
                        if (record[index] == 'A' && record[index + 1] == 'R' &&
                            record[index + 2] == 'M') {
                            saw_vendor = true;
                            break;
                        }
                    }
                    base::safety::Check(saw_vendor, "proc cpuinfo lacks the vendor line");
                    cinux::print::Println("[armv7] proc cpuinfo %u bytes", kBytes);
                    // The world door answers over the card the same way
                    // it answers over the x86 lane's own volume, with no
                    // second mount anywhere.
                    const auto kWorldShell = fs::FileWorld::self().resolve("/bin/sh");
                    cinux::print::Println(
                        kWorldShell.ok()
                            ? "[armv7] file world seated from the card, sh answers through it"
                            : "[armv7] file world seating FAILED");
                    const auto kFbView = kWorld.resolve("/dev/fb0");
        base::safety::Check(kFbView.ok(), "dev fb0 resolve failed");
        cinux::print::Println("[armv7] dev fb0 %ux%u",
                              driver::Framebuffer::self().width(),
                              driver::Framebuffer::self().height());
        // ptmx answers through the very vfs the syscalls use.
        const auto kPtmxView = kWorld.resolve("/dev/ptmx");
        base::safety::Check(kPtmxView.ok(), "dev ptmx resolve failed");
        cinux::print::Println("[armv7] dev ptmx seated");

        // The probe plants one key report and reads it back through
        // the very vfs.
        armv7::UartKeyboardEvents().publish(driver::kEvKey, 30U, 1);
        armv7::UartKeyboardEvents().publish(driver::kEvKey, 30U, 0);
        armv7::UartKeyboardEvents().publish_report();
        const auto kEventNode = kDev.lookup("event0");
        base::safety::Check(kEventNode.ok(), "dev event0 lookup failed");
        const auto kOpened = kDev.adopt_open(kEventNode.value(), "event0");
        base::safety::Check(kOpened.ok(), "dev event0 open failed");
        // Three records in this ABI's shape: the key press, the
        // release, and the report word that closes the frame.
        constexpr arch::abi::InputEventLayout kEventLayout = arch::abi::kInputEventLayout;
        unsigned char raw[3 * kEventLayout.bytes] = {};
        const auto             kChunk = kDev.read(kOpened.value(), 0, raw, sizeof(raw));
        base::safety::Check(kChunk.ok() && kChunk.value() == sizeof(raw),
                            "dev event0 read failed");
        cinux::print::Println("[armv7] dev event0 %u bytes",
                              static_cast<unsigned long>(kChunk.value()));

                }
            }
        }
    }
}


fs::Vfs& vfs() {
    // The lane no longer owns a tree: the file world grew this boot's
    // world itself, so the one neutral door names the card on this
    // port exactly as it names the volume on x86.
    return fs::FileWorld::self().vfs();
}

}  // namespace cinux::armv7::userfs

// kernel_vfs() answers through the file world alone: this lane adopts
// its own tree into it, so the one neutral door -- FileWorld::vfs() --
// names the card on this port exactly as it names the volume on x86.
