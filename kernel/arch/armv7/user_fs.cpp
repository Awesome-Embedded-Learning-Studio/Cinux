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
#include "kernel/boot/print.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/fs/file_table.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/arch/armv7/lcdif.hpp"
#include "kernel/fs/procfs/procfs.hpp"
#include "kernel/fs/devfs/devfs.hpp"
#include "kernel/fs/devfs/fb_node.hpp"
#include "kernel/drivers/device/device.hpp"
#include "kernel/drivers/framebuffer/framebuffer.hpp"
#include "kernel/mm/pmm.hpp"

namespace cinux::armv7::userfs {
namespace {

/// Heap-owned on purpose: a static would construct before the kernel
/// heap exists, and the RamFs root is heap-born.
fs::RamFs* g_ram  = nullptr;
fs::Vfs*   g_vfs  = nullptr;
fs::ProcFs* g_proc = nullptr;

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

/// The console's read: line discipline over the UART, kernel buffer in,
/// user buffer out. Returns the byte count; -1 on a refused copy.
long console_read(unsigned long buffer, unsigned long count) {
    unsigned long received = 0;
    while (received < count && cinux::armv7::UartReceiveReady()) {
        const char          kChar = cinux::armv7::UartReadChar();
        const unsigned char kByte = static_cast<unsigned char>(kChar);
        if (!cinux::arch::CopyToUser(buffer + received, &kByte, 1)) {
            return -1;
        }
        ++received;
        if (kChar == '\n' || kChar == '\r') {
            break;
        }
    }
    return static_cast<long>(received);
}

/// The console's write: chunked copies out of user memory onto the UART.
long console_write(unsigned long buffer, unsigned long count) {
    constexpr unsigned long kChunkBytes = 64;
    unsigned char           chunk[kChunkBytes];
    unsigned long           offset = 0;
    while (offset < count) {
        const unsigned long kTake =
            (count - offset) < kChunkBytes ? (count - offset) : kChunkBytes;
        if (!cinux::arch::CopyFromUser(chunk, buffer + offset, kTake)) {
            return -static_cast<long>(cinux::armv7::errno::kFault);
        }
        for (unsigned long index = 0; index < kTake; ++index) {
            cinux::armv7::UartPutChar(chunk[index]);
        }
        offset += kTake;
    }
    return static_cast<long>(count);
}

/// The erased console face. The node handle is empty: the console has
/// exactly one instance, so the backend pointer carries nothing and
/// every hook either forwards to the UART or is a no-op.
constexpr fs::FsOps kConsoleOps{
    /* lookup      = */ nullptr,
    /* make_file   = */ nullptr,
    /* make_dir    = */ nullptr,
    /* unlink      = */ nullptr,
    /* read        = */
    [](void*, fs::FsNode, unsigned long, void*, unsigned long) {
        return base::Result<unsigned long>{base::KernelError::kNotSupported};
    },
    /* write       = */
    [](void*, fs::FsNode, unsigned long, const void*, unsigned long) {
        return base::Result<unsigned long>{base::KernelError::kNotSupported};
    },
    /* read_dir    = */ nullptr,
    /* stat        = */
    [](void*, fs::FsNode, fs::FsStat& probed) {
        probed = fs::FsStat{};
        return base::Result<void>{};
    },
    /* retain      = */ [](void*, fs::FsNode) {},
    /* release     = */ [](void*, fs::FsNode) {},
    /* readable    = */ [](void*, fs::FsNode) { return true; },
    /* ioctl       = */ nullptr,
    /* adopt_open  = */
    [](void*, fs::FsNode node, std::string_view) {
        return base::Result<fs::FsNode>{node};
    },
};

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
        auto* const kDev = new fs::DevFs();
        driver::DeviceModel::self().bind(*kDev);
        fs::RegisterFramebufferDevice();
        const base::Result<void> kDevMount = g_vfs->mount("/dev", *kDev, "devtmpfs");
        base::safety::Check(kDevMount.ok(), "devfs mount failed");
        const auto kFbView = g_vfs->resolve("/dev/fb0");
        base::safety::Check(kFbView.ok(), "dev fb0 resolve failed");
        cinux::print::Println("[armv7] dev fb0 %ux%u",
                              driver::Framebuffer::self().width(),
                              driver::Framebuffer::self().height());
    }
}

fs::Vfs& vfs() {
    return *g_vfs;
}

fs::RamFs& ram() {
    return *g_ram;
}

const fs::FsOps* console_ops() {
    return &kConsoleOps;
}

}  // namespace cinux::armv7::userfs
