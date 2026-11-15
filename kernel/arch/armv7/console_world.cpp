/**
 * @file    console_world.cpp
 * @brief    The armv7 half of the console world seats.
 *
 * The board's own words: the UART keyboard is the console's first key
 * source with the USB keyboard behind it, the USB tablet is the
 * pointer when the controller finds one, and the card is the disk row
 * the device model learns about. The bring-up lane seats its own tree
 * the same way today; this half serves the day the file world's own
 * mounts run on this port.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch_armv7
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/console_world.hpp"

#include "kernel/arch/armv7/uart_input.hpp"
#include "kernel/drivers/device/device.hpp"
#include "kernel/drivers/tty/console_tty.hpp"
#include "kernel/fs/device_tree.hpp"
#include "kernel/drivers/usb/usb_keyboard.hpp"
#include "kernel/drivers/usb/usb_tablet.hpp"
#include "kernel/drivers/usdhc/usdhc.hpp"

namespace cinux::arch {

void SeatConsoleWorld(fs::DevFs& devices) {
    driver::SetConsoleKeyTap(driver::ConsoleKeyTap{
        .take = +[](char* out) {
            if (driver::UsbKeyboard::self().poll()) {
                *out = driver::UsbKeyboard::self().take();
                return true;
            }
            return false;
        },
        .has = +[] { return driver::UsbKeyboard::self().poll(); },
    });
    [[maybe_unused]] const bool kUsbKeyboard = driver::SeatUsbKeyboard();
    const bool                  kTablet      = driver::UsbTablet::self().init();
    fs::SeatDeviceTree(devices, armv7::UartKeyboardEvents(),
                       kTablet ? driver::UsbTablet::self().events()
                               : armv7::BoardPointerEvents());
    if (driver::UsdhcHost::self().block_count() != 0) {
        const base::Result<void> kMmc = driver::DeviceModel::self().seat({
            .klass       = driver::DeviceClass::kBlock,
            .name        = "mmcblk0",
            .number      = fs::MakeDeviceId(179, 0),
            .ops         = nullptr,
            .bus         = "usdhc",
            .driver_name = "usdhc",
        });
        base::safety::Check(kMmc.ok(), "device model refused the mmcblk0 seat");
    }
}

}  // namespace cinux::arch
