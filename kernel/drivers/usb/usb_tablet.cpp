#include "kernel/drivers/usb/usb_tablet.hpp"

#include <stdint.h>

#include "cinux/bit_ops/bitmask.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/drivers/input/input_config.hpp"
#include "kernel/drivers/usb/hcd.hpp"
#include "kernel/drivers/usb/xhci_layout.hpp"

namespace cinux::driver {
namespace {
constexpr uint16_t kAbsX      = 0;
constexpr uint16_t kAbsY      = 1;
constexpr uint16_t kBtnLeft   = 0x110;
constexpr uint16_t kBtnRight  = 0x111;
constexpr uint16_t kBtnMiddle = 0x112;

}

bool UsbTablet::init() {
    if (ready_) {
        return true;
    }
    usb::KeyboardEp endpoint{};
    for (usb::HostDriver* candidate : usb::Hosts()) {
        if (candidate != nullptr &&
            device_.enumerate(*candidate, usb::kHidProtocolNone, endpoint)) {
            host_ = candidate;
            break;
        }
    }
    if (host_ == nullptr) {
        return false;
    }
    if (endpoint.address != 0x81 || endpoint.max_packet < 5 ||
        !device_.set_configuration(*host_, endpoint.config_value) ||
        !device_.configure_interrupt_in(*host_, endpoint.max_packet, endpoint.interval)) {
        static_cast<void>(device_.detach(*host_));
        host_ = nullptr;
        return false;
    }
    stream_.configure_absolute(32767);
    stream_.set_poll_hook(+[] { UsbTablet::self().host_->pump(); });
    device_.interrupt_read(*host_, &UsbTablet::report_arrived, this);
    ready_ = true;
    print::Println("[kern] usb tablet seated, absolute axes 0..32767");
    return true;
}

void UsbTablet::report_arrived(UsbTablet& self, unsigned long status, unsigned long length) {
    if (status == 0 && length >= 5) {
        const TabletReport kReport = DecodeTablet(self.device_.report());
        self.stream_.publish(kEvAbs, kAbsX, kReport.x);
        self.stream_.publish(kEvAbs, kAbsY, kReport.y);
        constexpr uint16_t kButtons[] = {kBtnLeft, kBtnRight, kBtnMiddle};
        for (unsigned int index = 0; index < 3; ++index) {
            const auto kBit    = base::bit::MaskBit<uint8_t>(index);
            const bool kNow    = base::bit::BitMask<uint8_t>{kReport.buttons}.has(kBit);
            const bool kBefore = base::bit::BitMask<uint8_t>{self.buttons_}.has(kBit);
            if (kNow != kBefore) {
                self.stream_.publish(kEvKey, kButtons[index], kNow ? 1 : 0);
            }
        }
        self.buttons_ = kReport.buttons;
        self.stream_.publish_report();
    }
    if (self.host_ != nullptr) {
        self.device_.interrupt_read(*self.host_, &UsbTablet::report_arrived, &self);
    }
}

EventStream& UsbTablet::events() {
    return stream_;
}

}
