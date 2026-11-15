/**
 * @file    init_thread.cpp
 * @brief   The init thread: mount the volume, keep a shell alive.
 *
 * @author  Charliechen114514
 * @date    2026-10-12
 * @version 0.2
 * @since   0.1.0
 * @ingroup kernel_init
 * @copyright Copyright (c) 2026
 */

#include "kernel/init/init_thread.hpp"

#include <cstdint>
#include <string_view>
#include <utility>

#include "cinux/assert.hpp"
#include "cinux/math.hpp"
#include "cinux/memory.hpp"
#include "cinux/own.hpp"
#include "cinux/ptr.hpp"
#include "cinux/result.hpp"
#include "cinux/shared.hpp"
#include "kernel/arch/firmware.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/user_entry.hpp"
#include "kernel/arch/random.hpp"
#include "kernel/arch/space_control.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/drivers/tty/console_tty.hpp"
#include "kernel/fs/devfs/console_node.hpp"
#include "kernel/fs/file_world.hpp"
#include "kernel/fs/fs_config.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/loader/elf_layout.hpp"
#include "kernel/loader/loader.hpp"
#include "kernel/drivers/ahci/ahci.hpp"
#include "kernel/drivers/block/block_device_concept.hpp"
#include "kernel/loader/program_seat.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/net/net_config.hpp"
#include "kernel/net/net_world.hpp"
#include "kernel/proc/proc_config.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/signals.hpp"
#include "kernel/proc/task.hpp"

namespace cinux::init {

namespace {


/// The loaded shell's entry and root, staged for the capture-free trampoline.
/// The shell's landing facts, one home for the whole seat; Meyers
/// singleton, no bare globals.
class ShellSeats {
public:
    static ShellSeats& self() {
        static ShellSeats seats;
        return seats;
    }

    /// Where the shell's first instruction sits.
    std::uint64_t entry = 0;

    /// The shell world's top-level table.
    unsigned long root = 0;

    /// Where the shell's stack picture landed.
    unsigned long long rsp = 0;

    proc::Task* pending = nullptr;

private:
    ShellSeats() = default;
};

/// Reads one whole file off the mounted volume into an owned buffer,
/// handing back the resolved view for the caller's own lookups.
bool read_volume_file(const char* path, base::Own<unsigned char[]>& out, unsigned long& out_bytes,
                      fs::FsView& view) {
    const base::Result<fs::FsView> kView = fs::FileWorld::self().resolve(path);
    if (!kView.ok()) {
        return false;
    }
    view = kView.value();
    return fs::ReadFilePrefix(view, 65536, out, out_bytes).ok();
}

/// One built shell world: the space and its range register, seat-ready.
struct ShellWorld {
    base::Shared<mm::AddressSpace> space;
    base::Shared<mm::VmaStore>     vmas;
    unsigned long long             entry_rsp;  ///< Where the shell's stack picture landed.
};

/// Loads the volume's shell ELF into a fresh world with a stack page and a
/// full range register on top; an empty ShellWorld when any step refuses.
ShellWorld build_shell_world(std::uint64_t& entry) {
    unsigned long              program_bytes = 0;
    base::Own<unsigned char[]> program;
    fs::FsView                 view{.ops = nullptr, .self = nullptr, .rest = {}};
    if (!read_volume_file("/sbin/init", program, program_bytes, view)) {
        print::Println("[kern] kernel_init: /sbin/init unreadable on the volume");
        return {};
    }
    base::Shared<mm::AddressSpace> space(new mm::AddressSpace{});
    base::Shared<mm::VmaStore>     vmas(new mm::VmaStore{});
    if (!space.holds() || !vmas.holds() || !space->init()) {
        return {};
    }
    const base::Result<fs::FsNode> kNode = view.ops->lookup(view.self, view.rest);
    if (!kNode.ok()) {
        return {};
    }
    // The one ladder every image climbs: world assembly, entry facts,
    // the GNU stack flag, the process layout, the interpreter. The
    // first shell is seated exactly the way exec seats a replacement.
    const auto kSeat = loader::SeatProgram(program.get(), program_bytes, kNode.value(), view,
                                           *space, *vmas);
    if (!kSeat.ok()) {
        return {};
    }
    loader::EntryFacts facts = kSeat.value().facts;
    entry                    = facts.program_entry;
    const bool kStackExecutable = kSeat.value().stack_executable;
    unsigned char randomness[16]{};
    loader::DrawEntryRandom(randomness);
    facts.randomness   = randomness;
    facts.random_bytes = sizeof(randomness);
    const unsigned long long kStackTop = loader::DrawEntryStackTop();
    mm::VmaRegion stack_range{};
    stack_range.start      = kStackTop - mm::kUserStackBytes - cinux::arch::page::kSize;
    stack_range.end        = kStackTop;
    stack_range.kind       = mm::VmaKind::kStack;
    stack_range.readable   = true;
    stack_range.writable   = true;
    stack_range.executable = kStackExecutable;
    if (!vmas->register_range(stack_range).ok()) {
        return {};
    }
    const mm::Pmm::PhysAddr kStackFrame = mm::Pmm::self().allocate_page();
    if (kStackFrame == mm::Pmm::PhysAddr{}) {
        return {};
    }
    cinux::base::SetBytes(cinux::base::PtrAt<unsigned char>(mm::DirectMapVirt(kStackFrame.raw)), 0,
                          cinux::arch::page::kSize);
    const char* const kShellArgv[2] = {"/sbin/init", nullptr};
    char              boot_environment[128]{};
    static_cast<void>(arch::firmware::ReadString("opt/cinux/init-env", boot_environment));
    const auto        kSeparator          = std::string_view{boot_environment}.find('=');
    const bool        kEnvironmentPresent = kSeparator != std::string_view::npos && kSeparator != 0;
    const char* const kShellEnvp[]        = {
        "PATH=/bin:/sbin:/usr/bin:/usr/sbin",
        "HOME=/root",
        "TERM=linux",
        kEnvironmentPresent ? boot_environment : nullptr,
        nullptr,
    };
    const loader::EntryRuns  kShellRuns{.arguments = kShellArgv, .environment = kShellEnvp};
    const unsigned long long kEntryRsp =
        kStackTop - cinux::arch::page::kSize + proc::kUserEntryStackOffset;
    if (loader::WriteEntryPicture(
            cinux::base::PtrAt<unsigned char>(mm::DirectMapVirt(kStackFrame.raw)), kEntryRsp,
            kShellRuns, facts) == 0) {
        mm::Pmm::self().free_page(kStackFrame);
        return {};
    }
    if (!space->map_user(kStackTop - cinux::arch::page::kSize, kStackFrame.raw,
                         cinux::arch::page::UserLeafTerms(true, kStackExecutable))) {
        mm::Pmm::self().free_page(kStackFrame);
        return {};
    }
    if (!proc::MapSignalTrampoline(*space, *vmas)) {
        return {};
    }
    return ShellWorld{
        .space     = std::move(space),
        .vmas      = std::move(vmas),
        .entry_rsp = kEntryRsp,
    };
}

/// Seats one shell task over a freshly built world; the task owns the world
/// and its range register from here to their reclaim. False when the world
/// refused to build.
void enter_initial_process() {
    const ShellSeats& seats = ShellSeats::self();
    proc::Task* const kSelf = proc::Scheduler::self().current();
    kSelf->user_root        = seats.root;
    static_cast<void>(cinux::arch::SaveAndDisableIrq());
    cinux::arch::space::SwitchRoot(seats.root);
    cinux::arch::EnterUserMode(seats.entry, seats.rsp);
}

proc::Task* prepare_initial_process(proc::Task& parent) {
    auto* const kTask = proc::TaskBuilder{}
                            .set_entry(enter_initial_process)
                            .set_name("init")
                            .set_parent(parent)
                            .build();
    if (kTask != nullptr) {
        proc::Scheduler::self().seat(*kTask, false, true);
    }
    return kTask;
}

bool seat_shell_task(proc::Task& parent) {
    std::uint64_t entry = 0;
    ShellWorld    world = build_shell_world(entry);
    if (!world.space.holds() || !world.vmas.holds()) {
        return false;
    }
    ShellSeats::self().entry   = entry;
    ShellSeats::self().root    = world.space->root();
    ShellSeats::self().rsp     = world.entry_rsp;
    proc::Task* shell          = ShellSeats::self().pending;
    ShellSeats::self().pending = nullptr;
    if (shell == nullptr) {
        shell = prepare_initial_process(parent);
    }
    if (shell == nullptr) {
        return false;
    }
    shell->space = std::move(world.space);
    shell->vmas  = std::move(world.vmas);
    if (!fs::FileWorld::self().seat_console_streams(*shell->files).ok()) {
        return false;
    }
    shell->pgid            = shell->pid;
    shell->sid             = shell->pid;
    shell->controlling_tty = &driver::ConsoleTty();
    fs::AdoptConsoleForeground(shell->pgid);
    cinux::base::CopyBytes(shell->exe, "/sbin/init", sizeof("/sbin/init"));
    cinux::base::CopyBytes(shell->comm, "init", sizeof("init"));
    proc::Scheduler::self().unblock(*shell);
    return true;
}

}  // namespace

void StartInitThread() {
    auto kernel_init = +[] {
        // The root volume's door: one port adopted over the SATA host
        // this lane owns. A lane with another block device hands its
        // own port; the mount ladder above it is shared.
        static driver::BlockPort sata_port;
        sata_port.adopt(driver::Ahci::self());
        fs::FileWorld::self().init(sata_port);
        fs::StartConsoleInputPump();
        base::safety::Check(net::NetWorld::self().start_pump(), "net pump refused its seat");
        {
            const unsigned long kReplies =
                net::NetWorld::self().ping_sweep(net::kUserNetworkHost, 4);
            print::Println("[kern] kernel_init: ping 10.0.2.2 answered %u of 4, launching shell",
                           kReplies);
        }
        for (;;) {
            proc::Task* const kSelf = proc::Scheduler::self().current();
            if (!seat_shell_task(*kSelf)) {
                print::Println("[kern] kernel_init: shell refused, parking");
                return;
            }
            proc::ChildReceipt receipt{};
            proc::Scheduler::self().wait_child(-1, false, receipt);
            constexpr unsigned long long kSignalMask = 0x7F;
            const auto                   kStatus = static_cast<unsigned long long>(receipt.code);
            if ((kStatus & kSignalMask) != 0) {
                print::Println("[kern] kernel_init: shell killed by signal %llu, respawning",
                               kStatus & kSignalMask);
            } else {
                print::Println("[kern] kernel_init: shell exited with %llu, respawning",
                               (kStatus >> 8U) & 0xFFULL);
            }
        }
    };
    auto* const kInit = proc::TaskBuilder{}.set_entry(kernel_init).set_name("kernel_init").build();
    base::safety::Check(kInit != nullptr, "kernel_init task failed to build");
    ShellSeats::self().pending = prepare_initial_process(*kInit);
    base::safety::Check(ShellSeats::self().pending != nullptr, "initial process failed to build");
    proc::Scheduler::self().seat(*kInit, false);
    kInit->pgid = kInit->pid;
    kInit->sid  = kInit->pid;
    proc::Scheduler::self().unblock(*kInit);
}

}  // namespace cinux::init
