/**
 * @file    user_fs.cpp
 * @brief   Wiring the armv7 user world onto the shared fs stack.
 *
 * One RamFs volume seeded with the bring-up files sits behind one Vfs
 * root mount; the UART console appears as a character-device face so
 * descriptors 0-2 can live in the same FileTable as everything else.
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
#include "kernel/fs/fs_config.hpp"
#include "kernel/fs/ramfs/ramfs.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/fs/kernel_vfs.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/drivers/usb/usb_keyboard.hpp"
#include "kernel/drivers/usb/usb_tablet.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/fs/file_table.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/arch/armv7/lcdif.hpp"
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
#include "kernel/loader/loader.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/mm/pmm.hpp"

extern "C" {
extern unsigned char g_userprog_start[];
extern unsigned char g_userprog_end[];
}

namespace cinux::armv7::userfs {
namespace {

/// Heap-owned on purpose: a static would construct before the kernel
/// heap exists, and the RamFs root is heap-born.
fs::RamFs* g_ram  = nullptr;
fs::Vfs*   g_vfs  = nullptr;
fs::ProcFs* g_proc = nullptr;
driver::BlockQueue*  g_disk_queue  = nullptr;
driver::BlockCache*  g_disk_cache  = nullptr;
/// The erased seat under the disk queue; heap-born so it outlives this
/// function, since BlockQueue keeps a reference to it.
driver::BlockPort*   g_disk_port   = nullptr;
driver::BlockWindow* g_disk_window = nullptr;
fs::ext2::Ext2*          g_disk_volume = nullptr;
mm::AddressSpace* g_guest_space = nullptr;
mm::VmaStore*     g_guest_ranges = nullptr;
unsigned long     g_guest_entry  = 0;

/// Resolves the current task's program through this port's own VFS.
/// The /proc/self/exe seat answers from here, so a busybox applet that
/// re-execs itself finds the image the shell was seated from; without
/// it the node reads as an empty file and every non-NOEXEC applet
/// reports "not found".
bool proc_exe_view(fs::FsView& view, fs::FsNode& node) {
    const proc::Task* const kSelf = proc::Scheduler::self().current();
    if (kSelf == nullptr || kSelf->exe[0] != '/') {
        return false;
    }
    // A task whose own record is the magic path can only be asking the
    // window about itself: answer nothing rather than recurse.
    if (std::string_view{kSelf->exe} == "/proc/self/exe") {
        return false;
    }
    const base::Result<fs::FsView> kView = g_vfs->resolve(kSelf->exe);
    if (!kView.ok()) {
        return false;
    }
    view                                 = kView.value();
    const base::Result<fs::FsNode> kNode = view.ops->lookup(view.self, view.rest);
    if (!kNode.ok()) {
        return false;
    }
    node = kNode.value();
    return true;
}

unsigned long proc_exe_path(void* /*context*/, char* out, unsigned long cap) {
    const proc::Task* const kSelf = proc::Scheduler::self().current();
    if (kSelf == nullptr || cap == 0) {
        return 0;
    }
    unsigned long used = 0;
    while (used + 1 < cap && kSelf->exe[used] != '\0') {
        out[used] = kSelf->exe[used];
        ++used;
    }
    out[used] = '\0';
    return used;
}

bool proc_exe_bytes(void* /*context*/, unsigned long offset, void* sink, unsigned long count,
                    unsigned long& gave) {
    fs::FsView view{};
    fs::FsNode node{};
    if (!proc_exe_view(view, node)) {
        return false;
    }
    const base::Result<unsigned long> kGot = view.ops->read(view.self, node, offset, sink, count);
    if (!kGot.ok()) {
        return false;
    }
    gave = kGot.value();
    return true;
}

unsigned long proc_exe_size(void* /*context*/) {
    fs::FsView view{};
    fs::FsNode node{};
    if (!proc_exe_view(view, node)) {
        return 0;
    }
    fs::FsStat               probed{};
    const base::Result<void> kProbed = view.ops->stat(view.self, node, probed);
    return kProbed.ok() ? probed.size : 0;
}

}  // namespace

void init() {
    static bool seeded = false;
    if (seeded) {
        return;
    }
    seeded = true;
    g_ram = new fs::RamFs();
    g_vfs = new fs::Vfs();
    // The facts instance answers from the port's own identity decode,
    // the page manager, and the executable window; the seats this lane
    // cannot fill yet stay null and read as a quiet empty file.
    g_proc = new fs::ProcFs();
    g_proc->set_source(fs::ProcSource{
        .exe_path  = proc_exe_path,
        .exe_bytes = proc_exe_bytes,
        .exe_size  = proc_exe_size,
        .memory =
            [](void*, unsigned long& free_pages, unsigned long& total_pages) {
                free_pages  = mm::Pmm::self().free_page_count();
                total_pages = mm::Pmm::self().managed_page_count();
            },
        .memory_page_bytes = arch::page::kSize,
        .nth_cpu           = fs::NthCpuFromIdentity,
    });
    const base::Result<void> kProc = g_vfs->mount("/proc", *g_proc, "proc");
    base::safety::Check(kProc.ok(), "proc mount failed");
    // Read-back probe: /proc/cpuinfo must answer through the very vfs
    // the syscalls use, and the first record carries the port's vendor.
    const auto kView = g_vfs->resolve("/proc/cpuinfo");
    base::safety::Check(kView.ok(), "proc cpuinfo resolve failed");
    const auto kNode = kView.value().ops->lookup(kView.value().self, kView.value().rest);
    base::safety::Check(kNode.ok(), "proc cpuinfo lookup failed");
    char              record[96]{};
    const auto        kRead =
        kView.value().ops->read(kView.value().self, kNode.value(), 0, record, sizeof(record));
    base::safety::Check(kRead.ok() && kRead.value() != 0, "proc cpuinfo read failed");
    const unsigned long kBytes = kRead.value();
    bool                saw_vendor = false;
    for (unsigned long index = 0; index + 4 < kBytes; ++index) {
        if (record[index] == 'A' && record[index + 1] == 'R' && record[index + 2] == 'M') {
            saw_vendor = true;
            break;
        }
    }
    base::safety::Check(saw_vendor, "proc cpuinfo lacks the vendor line");
    cinux::print::Println("[armv7] proc cpuinfo %u bytes", kBytes);

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
        fs::DevFs* const kDev = new fs::DevFs();
        // The same tree both ports grow: console, fb0, ptmx and the
        // two input seats. event0 speaks the UART's keys, event1 sits
        // quiet: the pointer has no source on this board.
        // The pointer is the USB tablet when the controller finds one:
        // the board's own pointer stream has no source behind it, and a
        // desktop needs a cursor that moves. The board stream stays the
        // fallback for a machine without a tablet.
        const bool kTablet = driver::UsbTablet::self().init();
        fs::SeatDeviceTree(*kDev, armv7::UartKeyboardEvents(),
                           kTablet ? driver::UsbTablet::self().events()
                                   : armv7::BoardPointerEvents());
        // The USB keyboard walked its street at bring-up; the neutral
        // hand-over lands it in this tree as the usbkbd character node.
        if (driver::SeatUsbKeyboard()) {
            cinux::print::Println("[armv7] dev usbkbd seated");
        }
        // The block street joins the model the same way the x86 drives
        // do: facts only, no devfs leaf. The card is reached through
        // the erased BlockPort, so this seat never learns the driver's
        // type — exactly the face BlockQueue will adopt later.
        if (driver::UsdhcHost::self().block_count() != 0) {
            driver::BlockPort port;
            port.adopt(driver::UsdhcHost::self());
            static unsigned char kProbe[512] = {};
            const bool kRead = port.read_blocks(driver::Lba{0},
                                                driver::BlockSpan{1}, kProbe);
            const base::Result<void> kMmc = driver::DeviceModel::self().seat({
                .klass       = driver::DeviceClass::kBlock,
                .name        = "mmcblk0",
                .number      = fs::MakeDeviceId(179, 0),
                .ops         = nullptr,
                .bus         = "usdhc",
                .driver_name = "usdhc",
            });
            base::safety::Check(kMmc.ok(), "device model refused the mmcblk0 seat");
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
                // The volume street, the same ladder the x86 lane
                // climbs: one window over the partition, one queue,
                // one cache, one ext2. The first reader through the
                // queue drives it synchronously, so mounting works
                // before the scheduler ever runs.
                if (kEntry) {
                    g_disk_window = new driver::BlockWindow();
                    g_disk_window->adopt(driver::UsdhcHost::self(), kStart);
                    // The neutral ladder: the card is reached through the
                    // erased port, so the seat never learns the driver's
                    // type, and queue, cache and volume come from the share
                    // both ports climb. The port lives on the heap because
                    // the queue points back at it.
                    g_disk_port = new driver::BlockPort();
                    g_disk_port->adopt(*g_disk_window);
                    // The disk is the root, the same shape the x86 lane
                    // has always mounted: one block port, one volume, the
                    // "/" seat. The only way this port differs from its
                    // sibling is when the card becomes reachable -- here
                    // that is after the usdhc probe, not at init time.
                    // The ramfs never held the seat: it is the kernel's
                    // /tmp staging area, where the loader lane parks the
                    // guest image and anything else that lives only as
                    // long as the boot.
                    const base::Result<void> kStage =
                        g_vfs->mount("/tmp", *g_ram, "ramfs");
                    base::safety::Check(kStage.ok(), "tmp staging mount failed");
                    const fs::BlockVolume kVolume =
                        fs::MountBlockVolume(*g_vfs, "/", *g_disk_port);
                    g_disk_queue  = kVolume.queue;
                    g_disk_cache  = kVolume.cache;
                    g_disk_volume = kVolume.volume;
                    if (kVolume.mounted) {
                        const auto kMotd = g_vfs->resolve("/etc/motd");
                        base::Own<unsigned char[]> kMotdBytes;
                        unsigned long              kMotdSize = 0;
                        const bool kRead = kMotd.ok() &&
                                           fs::ReadWholeFile(kMotd.value(), kMotdBytes,
                                                             kMotdSize)
                                               .ok() &&
                                           kMotdSize != 0;
                        cinux::print::Println(
                            kRead ? "[armv7] ext2 mounted, motd is %lu bytes"
                                  : "[armv7] ext2 mounted, motd read FAILED",
                            kMotdSize);
                        // The shell needs no copy any more: /bin/sh on the
                        // card is a fast symlink over busybox, and the
                        // guest's ext2 walk expands it.
                        const auto kShellView = g_vfs->resolve("/bin/sh");
                        cinux::print::Println(kShellView.ok() ? "[armv7] bin/sh seated from the card"
                                                              : "[armv7] bin/sh missing on the card");
                    } else {
                        cinux::print::Println("[armv7] ext2 mount FAILED");
                    }
                }
            }
        }
        const base::Result<void> kDevMount = g_vfs->mount("/dev", *kDev, "devtmpfs");
        base::safety::Check(kDevMount.ok(), "devfs mount failed");
        const auto kFbView = g_vfs->resolve("/dev/fb0");
        base::safety::Check(kFbView.ok(), "dev fb0 resolve failed");
        cinux::print::Println("[armv7] dev fb0 %ux%u",
                              driver::Framebuffer::self().width(),
                              driver::Framebuffer::self().height());
        // ptmx answers through the very vfs the syscalls use.
        const auto kPtmxView = g_vfs->resolve("/dev/ptmx");
        base::safety::Check(kPtmxView.ok(), "dev ptmx resolve failed");
        cinux::print::Println("[armv7] dev ptmx seated");

        // The probe plants one key report and reads it back through
        // the very vfs.
        armv7::UartKeyboardEvents().publish(driver::kEvKey, 30U, 1);
        armv7::UartKeyboardEvents().publish(driver::kEvKey, 30U, 0);
        armv7::UartKeyboardEvents().publish_report();
        const auto kEventNode = kDev->lookup("event0");
        base::safety::Check(kEventNode.ok(), "dev event0 lookup failed");
        const auto kOpened = kDev->adopt_open(kEventNode.value(), "event0");
        base::safety::Check(kOpened.ok(), "dev event0 open failed");
        // Three records in this ABI's shape: the key press, the
        // release, and the report word that closes the frame.
        constexpr arch::abi::InputEventLayout kEventLayout = arch::abi::kInputEventLayout;
        unsigned char raw[3 * kEventLayout.bytes] = {};
        const auto             kChunk = kDev->read(kOpened.value(), 0, raw, sizeof(raw));
        base::safety::Check(kChunk.ok() && kChunk.value() == sizeof(raw),
                            "dev event0 read failed");
        cinux::print::Println("[armv7] dev event0 %u bytes",
                              static_cast<unsigned long>(kChunk.value()));

        // The loader lane: the guest ELF rides the blob into ramfs,
        // then the very loader x86 execs through reads it back and
        // maps its segments into a fresh user space.
        const unsigned long kImageBytes =
            static_cast<unsigned long>(g_userprog_end - g_userprog_start);
        const base::Result<fs::FsNode> kGuest = g_ram->make_file("guest.elf");
        base::safety::Check(kGuest.ok(), "guest elf staging refused");
        const auto kSeated = g_ram->write(kGuest.value(), 0,
                                          reinterpret_cast<const char*>(g_userprog_start),
                                          kImageBytes);
        base::safety::Check(kSeated.ok() && kSeated.value() == kImageBytes,
                            "guest elf staging short");
        const auto kGuestView = g_vfs->resolve("/tmp/guest.elf");
        base::safety::Check(kGuestView.ok(), "guest elf resolve failed");
        const auto kGuestNode =
            kGuestView.value().ops->lookup(kGuestView.value().self, kGuestView.value().rest);
        base::safety::Check(kGuestNode.ok(), "guest elf lookup failed");
        unsigned char* image = new unsigned char[kImageBytes];
        unsigned long  taken = 0;
        while (taken < kImageBytes) {
            const auto kChunk2 =
                kGuestView.value().ops->read(kGuestView.value().self, kGuestNode.value(), taken,
                                             image + taken, kImageBytes - taken);
            base::safety::Check(kChunk2.ok() && kChunk2.value() != 0, "guest elf read failed");
            taken += static_cast<unsigned long>(kChunk2.value());
        }
        g_guest_space = new mm::AddressSpace{};
        base::safety::Check(g_guest_space != nullptr && g_guest_space->init(),
                            "guest elf address space refused to init");
        g_guest_ranges = new mm::VmaStore{};
        const loader::LoadWorld kLoadWorld{
            .space         = *g_guest_space,
            .vmas          = *g_guest_ranges,
            .file          = kGuestNode.value(),
            .file_bytes    = kImageBytes,
            .file_ops      = kGuestView.value().ops,
            .file_self     = kGuestView.value().self,
            .file_lifetime = kGuestView.value().lifetime,
        };
        loader::EntryFacts facts{};
        base::safety::Check(loader::LoadElfIntoSpace(image, taken, kLoadWorld, facts),
                            "guest elf refused the neutral loader");
        loader::SeedProcessLayout(*g_guest_ranges);
        base::safety::Check(facts.program_entry >= 0x40000000U &&
                                facts.program_entry < 0x40100000UL,
                            "guest elf entry did not land where it was linked");
        // One stack page above the mmap base, inside the user window.
        const auto kStackFrame = mm::Pmm::self().allocate_page();
        base::safety::Check(kStackFrame.raw != 0 &&
                                g_guest_space->map_user(0x40010000UL, kStackFrame.raw),
                            "guest stack refused its mapping");
        g_guest_entry = static_cast<unsigned long>(facts.program_entry);
        cinux::print::Println("[armv7] load guest entry %X phnum %u", g_guest_entry,
                              facts.header_count);
    }
}

mm::VmaStore* take_guest_ranges() {
    // The seated task adopts the register; this hand-off is one-way,
    // so the global stops aliasing what the task's Shared will free.
    mm::VmaStore* const kTaken = g_guest_ranges;
    g_guest_ranges              = nullptr;
    return kTaken;
}

mm::AddressSpace* take_guest() {
    // Same one-way hand-off as the register: the task's Shared owns
    // the teardown from here on.
    mm::AddressSpace* const kTaken = g_guest_space;
    g_guest_space                    = nullptr;
    return kTaken;
}

unsigned long guest_entry() {
    return g_guest_entry;
}



fs::Vfs& vfs() {
    return *g_vfs;
}

fs::RamFs& ram() {
    return *g_ram;
}

}  // namespace cinux::armv7::userfs

namespace cinux::fs {
Vfs& kernel_vfs() {
    return cinux::armv7::userfs::vfs();
}
}  // namespace cinux::fs
