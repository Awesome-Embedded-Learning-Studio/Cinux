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
#include "kernel/arch/page.hpp"
#include "kernel/fs/file_table.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/arch/armv7/lcdif.hpp"
#include "kernel/arch/armv7/uart_input.hpp"
#include "kernel/fs/procfs/procfs.hpp"
#include "kernel/fs/devfs/devfs.hpp"
#include "kernel/fs/device_tree.hpp"
#include "kernel/fs/devfs/fb_node.hpp"
#include "kernel/fs/devfs/input_node.hpp"
#include "kernel/drivers/input/event_stream.hpp"
#include "kernel/drivers/input/input_config.hpp"
#include "kernel/drivers/device/device.hpp"
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
mm::AddressSpace* g_guest_space = nullptr;
mm::VmaStore*     g_guest_ranges = nullptr;
unsigned long     g_guest_entry  = 0;

/// One seeded regular file: create it, then lay its bytes in. The plain
/// make_file hands back a borrowed handle, so nothing is released here.
void seed_file(const char* name, const char* text, unsigned long bytes) {
    const base::Result<fs::FsNode> kMade = g_ram->make_file(name);
    base::safety::Check(kMade.ok(), "seed file refused");
    if (bytes != 0) {
        const base::Result<unsigned long> kLaid =
            g_ram->write(kMade.value(), 0, text, bytes);
        base::safety::Check(kLaid.ok(), "seed write refused");
    }
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
    const char kHello[]  = "hello\n";
    const char kMotd[]   = "hi there\n";
    const char kReadme[] = "readme!\n";
    seed_file("hello", kHello, sizeof(kHello) - 1);
    seed_file("motd", kMotd, sizeof(kMotd) - 1);
    seed_file("readme", kReadme, sizeof(kReadme) - 1);
    seed_file("scratch", "", 0);
    const base::Result<void> kRoot = g_vfs->mount("/", *g_ram, "ramfs");
    base::safety::Check(kRoot.ok(), "root mount failed");
    // The facts instance answers from the port's own identity decode
    // and the page manager; every seat the arm lane cannot fill yet
    // stays null and reads as a quiet empty file.
    g_proc = new fs::ProcFs();
    g_proc->set_source(fs::ProcSource{
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
        fs::SeatDeviceTree(*kDev, armv7::UartKeyboardEvents(), armv7::BoardPointerEvents());
        // The USB keyboard walked its street at bring-up; the neutral
        // hand-over lands it in this tree as the usbkbd character node.
        if (driver::SeatUsbKeyboard()) {
            cinux::print::Println("[armv7] dev usbkbd seated");
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
        driver::InputEventWire wire[3]{};
        const auto kChunk = kDev->read(kOpened.value(), 0, wire, sizeof(wire));
        base::safety::Check(kChunk.ok() && kChunk.value() == sizeof(wire),
                            "dev event0 read failed");
        base::safety::Check(wire[0].type == driver::kEvKey && wire[0].code == 30 &&
                                wire[0].value == 1 && wire[2].type == 0 && wire[2].code == 0,
                            "dev event0 wire lacked the key report");
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
        const auto kGuestView = g_vfs->resolve("/guest.elf");
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
