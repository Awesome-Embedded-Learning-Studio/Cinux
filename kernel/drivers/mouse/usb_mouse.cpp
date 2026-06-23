/**
 * @file kernel/drivers/mouse/usb_mouse.cpp
 * @brief UsbMouse: HID boot-mouse bring-up + report poll over an xHCI slot
 *
 * Collects the mouse-specific concerns that previously leaked into XhciSlot:
 * SET_PROTOCOL (HID class request), the report buffer, and the boot-mouse
 * decode.  Transport (control transfer, Configure Endpoint, interrupt-IN poll)
 * is delegated to XhciSlot.
 *
 * Namespace: cinux::drivers
 */

#include "usb_mouse.hpp"

#include <stdint.h>

#include "kernel/drivers/dma/dma_pool.hpp"
#include "kernel/drivers/usb/usb_descriptor.hpp"
#include "kernel/drivers/usb/usb_request.hpp"
#include "kernel/drivers/usb/xhci_controller.hpp"
#include "kernel/drivers/usb/xhci_slot.hpp"

namespace cinux::drivers {
using namespace cinux::drivers::usb;

namespace {
constexpr uint64_t kReportBytes = 8;  ///< boot-mouse report (<= max_packet, typically 4)

void zero_bytes(void* p, uint64_t n) {
    auto* b = static_cast<uint8_t*>(p);
    for (uint64_t i = 0; i < n; ++i) {
        b[i] = 0;
    }
}
}  // namespace

cinux::lib::ErrorOr<void> UsbMouse::init(XHCIController& hc, const BootMouseEp& ep) {
    auto rb = dma::g_dma_pool.alloc(kReportBytes);
    if (!rb.ok()) {
        return cinux::lib::Error::OutOfMemory;
    }
    report_buf_ = std::move(rb.value());
    zero_bytes(report_buf_.virt(), kReportBytes);
    report_len_ = static_cast<uint32_t>(kReportBytes);

    // SET_PROTOCOL(boot) on the HID interface (class control transfer on EP0).
    const UsbSetup sp =
        make_setup(bm_request_type(UsbDir::kOut, UsbReqType::kClass, UsbRecipient::kInterface),
                   UsbHid::kSetProtocol, /*protocol=*/0, ep.interface_number, 0);
    auto r = slot_->control_out_no_data(hc, sp);
    if (!r.ok()) {
        return r.error();
    }

    // Add the interrupt-IN endpoint via Configure Endpoint.
    return slot_->add_interrupt_endpoint(hc, ep.ep_number, ep.max_packet_size, ep.interval);
}

cinux::lib::ErrorOr<HidMouseReport> UsbMouse::poll(XHCIController& hc) {
    zero_bytes(report_buf_.virt(), kReportBytes);
    auto n = slot_->poll_interrupt_in(hc, report_buf_.phys(), report_len_);
    if (!n.ok()) {
        return n.error();  // TimedOut = idle mouse NAK
    }
    return decode_boot_mouse(static_cast<const uint8_t*>(report_buf_.virt()));
}

}  // namespace cinux::drivers
