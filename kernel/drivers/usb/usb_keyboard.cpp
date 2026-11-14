/**
 * @file    usb_keyboard.cpp
 * @brief   The class driver: find the endpoint, translate the reports.
 *
 * Enumeration reads the descriptor chain over the device's control
 * face, and every report completion arrives through the typed
 * callback — the report handler is a static member of this class, so
 * the owner rides in the signature, not in a void word.
 *
 * @author  Charliechen114514
 * @date    2026-10-22
 * @version 0.2
 * @since   0.6.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#include "kernel/drivers/usb/usb_keyboard.hpp"

#include <stdint.h>

#include "cinux/memory.hpp"
#include "cinux/result.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/drivers/device/device.hpp"
#include "kernel/drivers/usb/hcd.hpp"

#include "kernel/fs/devfs/devfs.hpp"

namespace cinux::driver {

namespace {

base::Result<unsigned long> glyph_read(void* /*context*/, void* sink, unsigned long count) {
    if (!UsbKeyboard::self().poll()) {
        return base::KernelError::kWouldBlock;
    }
    auto* const kOut = static_cast<unsigned char*>(sink);
    for (unsigned long index = 0; index < count; ++index) {
        const char kGlyph = UsbKeyboard::self().take();
        if (kGlyph == 0) {
            return index;
        }
        kOut[index] = static_cast<unsigned char>(kGlyph);
    }
    return count;
}

base::Result<unsigned long> accept_write(void* /*context*/, const void* /*source*/,
                                         unsigned long count) {
    return count;
}

bool glyph_has_input(void* /*context*/) {
    return UsbKeyboard::self().poll();
}

const fs::DeviceOps kKeyboardOps{
    .read      = glyph_read,
    .write     = accept_write,
    .has_input = glyph_has_input,
    .ioctl     = nullptr,
    .release   = nullptr,
    .context   = nullptr,
};


}  // namespace

bool UsbKeyboard::init() {
    if (device_.seated()) {
        return true;
    }
    usb::HostDriver* host = usb::PrimaryHost();
    if (host == nullptr) {
        return false;
    }
    usb::KeyboardEp keyboard{};
    if (!device_.enumerate(*host, 1, keyboard)) {
        return false;
    }
    if (keyboard.address != 0x81 || !device_.set_configuration(*host, keyboard.config_value)) {
        static_cast<void>(device_.detach(*host));
        return false;
    }
    if (!device_.configure_interrupt_in(*host, keyboard.max_packet, keyboard.interval)) {
        static_cast<void>(device_.detach(*host));
        return false;
    }
    device_.interrupt_read(*host, &UsbKeyboard::report_arrived, this);
    print::Println("[kern] usb keyboard seated, config %u ep %X max %u interval %u",
                   keyboard.config_value, keyboard.address, keyboard.max_packet, keyboard.interval);
    return true;
}

bool UsbKeyboard::seated() const {
    return device_.seated();
}

void UsbKeyboard::report_arrived(UsbKeyboard& self, unsigned long status, unsigned long length) {
    if (status == 0 && length == sizeof(self.previous_)) {
        const unsigned char* report = self.device_.report();
        const char           kGlyph = usb::HidReportKey(report, self.previous_);
        base::CopyBytes(self.previous_, report, sizeof(self.previous_));
        if (kGlyph != 0) {
            self.events_.push(kGlyph);
        }
    }
    if (usb::HostDriver* kHost = usb::PrimaryHost(); kHost != nullptr) {
        self.device_.interrupt_read(*kHost, &UsbKeyboard::report_arrived, &self);
    }
}

bool UsbKeyboard::poll() {
    if (usb::HostDriver* kHost = usb::PrimaryHost(); kHost != nullptr) {
        kHost->pump();
    }
    return events_.count() != 0;
}

char UsbKeyboard::take() {
    poll();
    char code = 0;
    events_.pop(code);
    return code;
}

namespace {
/// One devfs hand-over per boot: a second ask answers what the first won.
bool kUsbKeyboardSeated = false;
}  // namespace

bool SeatUsbKeyboard() {
    if (!UsbKeyboard::self().init()) {
        return false;
    }
    // A lane that walked the street at bring-up already holds its seat;
    // this is only the devfs hand-over.
    if (UsbKeyboard::self().seated() && kUsbKeyboardSeated) {
        return true;
    }
    const base::Result<void> kSeat = DeviceModel::self().seat({
        .klass       = DeviceClass::kCharacter,
        .name        = "usbkbd",
        .number      = fs::MakeDeviceId(10, 0),
        .ops         = &kKeyboardOps,
        .bus         = "usb",
        .driver_name = "xhci-hid",
    });
    kUsbKeyboardSeated = kSeat.ok();
    return kSeat.ok();
}

}  // namespace cinux::driver
