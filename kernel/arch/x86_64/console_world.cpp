/**
 * @file    console_world.cpp
 * @brief    The x86_64 half of the console world seats.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch_x86_64
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/console_world.hpp"

#include "kernel/drivers/device/device.hpp"
#include "kernel/drivers/tty/console_tty.hpp"
#include "kernel/fs/device_tree.hpp"
#include "kernel/drivers/keyboard/keyboard.hpp"
#include "kernel/drivers/usb/usb_keyboard.hpp"
#include "kernel/drivers/usb/usb_tablet.hpp"
#include "kernel/drivers/mouse/ps2_mouse.hpp"
#include "kernel/drivers/nvme/nvme.hpp"

namespace cinux::arch {

void SeatConsoleWorld(fs::DevFs& devices) {
    // The console key tap: the PS/2 keyboard first, the USB keyboard
    // behind it. Both spellings stay this lane's words; the tap face
    // they feed is shared.
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
    fs::SeatDeviceTree(devices, driver::Keyboard::self().events(),
                       kTablet ? driver::UsbTablet::self().events()
                               : driver::Ps2Mouse::self().events());
    const base::Result<void> kSata = driver::DeviceModel::self().seat({
        .klass       = driver::DeviceClass::kBlock,
        .name        = "sata0",
        .number      = fs::MakeDeviceId(8, 0),
        .ops         = nullptr,
        .bus         = "pci",
        .driver_name = "ahci",
    });
    base::safety::Check(kSata.ok(), "device model refused the sata0 seat");
    if (driver::Nvme::self().ready()) {
        const auto kNvme = driver::DeviceModel::self().seat({
            .klass       = driver::DeviceClass::kBlock,
            .name        = "nvme0",
            .number      = fs::MakeDeviceId(259, 0),
            .ops         = nullptr,
            .bus         = "pci",
            .driver_name = "nvme",
        });
        base::safety::Check(kNvme.ok(), "device model refused the nvme0 seat");
    }
}

}  // namespace cinux::arch
