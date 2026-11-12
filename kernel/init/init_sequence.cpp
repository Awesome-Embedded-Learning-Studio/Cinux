#include "kernel/init/init_sequence.hpp"

#include "cinux/assert.hpp"
#include "cinux/result.hpp"
#include "kernel/arch/init.hpp"
#include "kernel/arch/halt.hpp"
#include "kernel/boot/boot_info.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/console/screen.hpp"
#include "kernel/arch/x86_64/platform/acpi/acpi.hpp"
#include "kernel/drivers/ahci/ahci.hpp"
#include "kernel/drivers/device/device.hpp"
#include "kernel/platform/clock.hpp"
#include "kernel/drivers/keyboard/keyboard.hpp"
#include "kernel/drivers/mouse/ps2_mouse.hpp"
#include "kernel/drivers/net/e1000.hpp"
#include "kernel/drivers/net/loopback.hpp"
#include "kernel/drivers/nvme/nvme.hpp"
#include "kernel/drivers/nvme/nvme_config.hpp"
#include "kernel/arch/x86_64/platform/pci/pci.hpp"
#include "kernel/arch/x86_64/platform/pci/pci_layout.hpp"
#include "kernel/drivers/serial/serial.hpp"
#include "kernel/drivers/usb/usb_keyboard.hpp"
#include "kernel/drivers/usb/xhci.hpp"
#include "kernel/interrupt/irq.hpp"
#include "kernel/mm/heap_runtime.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vmm.hpp"
#include "kernel/net/net_world.hpp"
#include "kernel/proc/fault_diagnose.hpp"
#include "kernel/proc/fault_resolve.hpp"
#include "kernel/proc/lockdep.hpp"
#include "kernel/syscall/uaccess.hpp"
#include "kernel/time/tick.hpp"

namespace cinux::init {

namespace {

using boot::BootInfo;

constexpr InitStep kInitSteps[] = {
    {
        .name = "serial",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            driver::SerialInit();
            return info;
        },
    },
    {
        .name = "pmm",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            return mm::Pmm::self().init(*info) ? info : nullptr;
        },
    },
    {
        .name = "address space",
        .run =
            +[](const BootInfo* info) -> const BootInfo* { return mm::BringUpAddressSpace(*info); },
    },
    {
        .name = "page fault resolver",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            proc::InstallFaultResolver();
            proc::InstallFaultDiagnoser();
            proc::InstallLockdepSink(+[](const char* label, unsigned long long address) {
                if (label != nullptr) {
                    print::Println("[lockdep] held: %s", label);
                } else {
                    print::Println("[lockdep] held: lock@%llx", address);
                }
            });
            return info;
        },
    },
    {
        .name = "console",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            console::TextConsole::self().init(info->framebuffer);
            return info;
        },
    },
    {
        .name = "heap",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            return mm::BringUpHeap() ? info : nullptr;
        },
    },
    {
        .name = "interrupt controller",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            return arch::init::BringUpInterruptController(*info) ? info : nullptr;
        },
    },
    {
        .name = "legacy interrupt controller",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            arch::init::RemapLegacyInterruptController();
            return info;
        },
    },
    {
        .name = "fast system calls",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            arch::init::EnableFastPaths();
            return info;
        },
    },
    {
        .name = "kernel random",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            arch::init::SeedKernelRandom();
            return info;
        },
    },
    {
        .name = "core protections",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            arch::init::EnableCoreProtections();
            return info;
        },
    },
    {
        .name = "tick",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            time::Tick::self().init(driver::Pit::self());
            return info;
        },
    },
    {
        .name = "irq stubs",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            arch::init::InstallInterruptStubs();
            return info;
        },
    },
    {
        .name = "interrupt delivery",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            return arch::init::InstallInterruptDelivery() ? info : nullptr;
        },
    },
    {
        .name = "irq line 0",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            interrupt::Irq::self().enable_line(interrupt::IrqLine{.value = 0});
            return info;
        },
    },
    {
        .name = "keyboard",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            if (!driver::Keyboard::self().init()) {
                return nullptr;
            }
            driver::Keyboard::self().attach();
            return info;
        },
    },
    {
        .name = "pointer",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            static_cast<void>(driver::Ps2Mouse::self().init());
            return info;
        },
    },
    {
        .name = "pci",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            driver::PciBus::self().init();
            return info;
        },
    },
    {
        .name = "interrupts",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            arch::init::EnableInterrupts();
            return info;
        },
    },
    {
        .name = "ahci",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            driver::Ahci::self().init();
            return info;
        },
    },
    {
        .name = "hpet",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            if (!driver::Hpet::self().init(driver::acpi::FindRootPointer())) {
                print::Println("[kern] hpet absent, clocks stay tick-grained");
            }
            return info;
        },
    },
    {
        .name = "nvme",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            if (driver::PciBus::self().find_by_class(driver::PciClassCode::kMassStorage,
                                                     driver::kNvmeSubclass) != nullptr) {
                base::safety::Check(driver::Nvme::self().init(), "NVMe refused initialization");
                if (driver::Nvme::self().scratch_namespace()) {
                    base::safety::Check(driver::Nvme::self().verify_io(),
                                        "NVMe I/O verification failed");
                }
            }
            return info;
        },
    },
    {
        .name = "xhci host",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            if (!driver::XhciHost::self().init()) {
                print::Println("[kern] xhci absent, usb stays dark");
            }
            return info;
        },
    },
    {
        .name = "usb keyboard",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            if (!driver::UsbKeyboard::self().init() && driver::XhciHost::self().present()) {
                print::Println("[kern] usb keyboard refused enumeration, ps/2 carries the typing");
            }
            return info;
        },
    },
    {
        .name = "network loopback",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            driver::net::Loopback::self().init();
            const base::Result<void> kLo = driver::DeviceModel::self().seat({
                .klass       = driver::DeviceClass::kNetwork,
                .name        = "lo",
                .number      = 0,
                .ops         = nullptr,
                .bus         = "virtual",
                .driver_name = "loopback",
            });
            base::safety::Check(kLo.ok(), "device model refused the loopback seat");
            net::NetWorld::self().init();
            print::Println("[net] loopback stack wired, pump seats with init");
            if (driver::net::E1000::self().init()) {
                net::NetWorld::self().attach_wire(driver::net::E1000::self().port());
                const auto kMac = driver::net::E1000::self().mac();
                print::Println("[net] e1000 up, mac %02x:%02x:%02x:%02x:%02x:%02x", kMac.bytes[0],
                               kMac.bytes[1], kMac.bytes[2], kMac.bytes[3], kMac.bytes[4],
                               kMac.bytes[5]);
            } else {
                print::Println("[net] no e1000 answered, loopback only");
            }
            return info;
        },
    },
    {
        .name = "secondary processors",
        .run  = +[](const BootInfo* info) -> const BootInfo* {
            return arch::init::BootSecondaryProcessors() ? info : nullptr;
        },
    },
};

}  // namespace

const boot::BootInfo* RunInitSequence(const boot::BootInfo* info) {
    for (const InitStep& step : kInitSteps) {
        auto const* const kNext = step.run(info);
        if (kNext == nullptr) {
            print::Println("[kern] init '%s' failed, halting", step.name);
            arch::Halt();
        }
        info = kNext;
    }
    return info;
}

}  // namespace cinux::init
