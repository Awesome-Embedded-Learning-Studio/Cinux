/**
 * @file    usb_device.cpp
 * @brief   The device layer's walk, one class driver's type at a time.
 *
 * The enumeration walk, the descriptor reads, and the report page's
 * arming live here; every register word stays behind the neutral
 * HostDriver face. Explicit instantiation happens at the class
 * drivers that own devices.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.3
 * @since   0.6.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#include "kernel/drivers/usb/usb_device.hpp"

#include <stdint.h>

#include "cinux/memory.hpp"
#include "cinux/byte_order.hpp"
#include "cinux/ptr.hpp"
#include "kernel/drivers/usb/usb_config.hpp"
#include "kernel/drivers/usb/xhci_layout.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/pmm.hpp"

namespace cinux::driver::usb {

namespace {

/// @brief One page of zeros, the honest size of a DMA page.
constexpr unsigned long kPageBytes = 4096;

/// @brief Where the report sits inside its page.
constexpr unsigned long kReportOffset = 0x800;

}  // namespace

template <typename Owner>
bool UsbDevice<Owner>::detach(HostDriver& host) {
    if (seat_ == nullptr) {
        return true;
    }
    host.detach(seat_);
    if (report_page_ != 0) {
        mm::Pmm::self().free_page(base::PhysAddr{static_cast<unsigned long>(report_page_)});
        report_page_ = 0;
    }
    seat_ = nullptr;
    return true;
}

template <typename Owner>
bool UsbDevice<Owner>::enumerate(HostDriver& host, uint8_t protocol, KeyboardEp& endpoint) {
    if (!host.present()) {
        return false;
    }
    for (unsigned int port = 1; port <= host.max_ports() && port <= 255; ++port) {
        if (host.port_taken(port) || !host.port_connected(port)) {
            continue;
        }
        seat_ = host.address(port);
        if (seat_ == nullptr) {
            return false;
        }
        unsigned char descriptor[256] = {};
        if (host.control_read(seat_, 0x0100, descriptor, 18) &&
            host.control_read(seat_, 0x0200, descriptor, 9)) {
            const auto kLength = base::ReadWord16(descriptor, 2);
            if (kLength >= 9 && kLength <= sizeof(descriptor) &&
                host.control_read(seat_, 0x0200, descriptor, kLength) &&
                FindHidInterface(descriptor, kLength, protocol, endpoint)) {
                return true;
            }
        }
        if (!detach(host)) {
            return false;
        }
    }
    return false;
}

template <typename Owner>
bool UsbDevice<Owner>::set_configuration(HostDriver& host, uint8_t config_value) {
    return host.set_configuration(seat_, config_value);
}

template <typename Owner>
bool UsbDevice<Owner>::configure_interrupt_in(HostDriver& host, uint16_t max_packet,
                                              uint8_t interval) {
    if (interval == 0) {
        return false;
    }
    return host.configure_interrupt_in(seat_, max_packet, interval);
}

template <typename Owner>
void UsbDevice<Owner>::interrupt_read(HostDriver& host, ReportHandler handler, Owner* owner) {
    if (seat_ == nullptr) {
        return;
    }
    if (report_page_ == 0) {
        const base::PhysAddr kPage = mm::Pmm::self().allocate_page();
        if (kPage == base::PhysAddr{}) {
            return;
        }
        base::SetBytes(base::PtrAt<void>(mm::DirectMapVirt(kPage.raw)), 0, kPageBytes);
        report_page_ = kPage.raw;
    }
    report_call_.arm(handler, owner);
    host.arm_interrupt_in(seat_, report_page_ + kReportOffset, kUsbReportBytes,
                          &UsbDevice::trampoline, this);
}

template <typename Owner>
void UsbDevice<Owner>::trampoline(void* device, const TransferDone& done) {
    auto& kSelf = *static_cast<UsbDevice*>(device);
    kSelf.report_call_.invoke(done.ok ? 0UL : 1UL, done.length);
}

template <typename Owner>
const unsigned char* UsbDevice<Owner>::report() const {
    return base::PtrAt<const unsigned char>(mm::DirectMapVirt(report_page_ + kReportOffset));
}

template <typename Owner>
bool UsbDevice<Owner>::seated() const {
    return seat_ != nullptr;
}

}  // namespace cinux::driver::usb

namespace cinux::driver {
class UsbKeyboard;
class UsbTablet;
}

template class cinux::driver::usb::UsbDevice<cinux::driver::UsbKeyboard>;
template class cinux::driver::usb::UsbDevice<cinux::driver::UsbTablet>;
