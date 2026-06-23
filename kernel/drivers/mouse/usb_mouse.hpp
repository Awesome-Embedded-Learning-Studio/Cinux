/**
 * @file kernel/drivers/mouse/usb_mouse.hpp
 * @brief USB HID boot-mouse driver over an xHCI slot
 *
 * The HID-mouse APPLICATION layer: owns the report buffer + the mouse's
 * interrupt-IN endpoint configuration, polls the endpoint through the generic
 * XhciSlot transport (drivers/usb/), and decodes the boot-mouse report
 * (hid.hpp, co-located here).  Lives under drivers/mouse/ for input-subsystem
 * cohesion alongside the PS/2 Mouse sink; depends one-way on drivers/usb/ for
 * transport only.
 *
 * The decoded report is fed to Mouse::inject_usb_motion (dy NOT inverted).
 *
 * Namespace: cinux::drivers
 */

#pragma once

#include <stdint.h>

#include <cinux/expected.hpp>

#include "hid.hpp"
#include "kernel/drivers/dma/dma_buffer.hpp"

namespace cinux::drivers::usb {
class XhciSlot;
class XHCIController;
}  // namespace cinux::drivers::usb

namespace cinux::drivers {

/// USB HID boot mouse bound to one xHCI slot.  Owns its report buffer + the
/// interrupt-IN endpoint config; uses XhciSlot for transport + hid.hpp for
/// decode.  A worker polls poll() and forwards the report to Mouse.
class UsbMouse {
public:
    /// Bind to the xHCI slot carrying this mouse's USB device.
    void bind(usb::XhciSlot& slot) { slot_ = &slot; }

    /// Bring up the HID boot mouse: allocate the report buffer, SET_PROTOCOL
    /// (boot) on the interface, and add the interrupt-IN endpoint (Configure
    /// Endpoint).  @p ep from find_boot_mouse() over the config descriptor.
    cinux::lib::ErrorOr<void> init(usb::XHCIController& hc, const usb::BootMouseEp& ep);

    /// Poll once: fetch a report from the interrupt-IN EP and decode it.
    /// Returns Error::TimedOut when the mouse is idle (NAK) -- correct
    /// interrupt-IN behaviour, not an error.
    cinux::lib::ErrorOr<usb::HidMouseReport> poll(usb::XHCIController& hc);

private:
    usb::XhciSlot*                 slot_ = nullptr;
    cinux::drivers::dma::DmaBuffer report_buf_;      ///< DMA report buffer (controller writes here)
    uint32_t                       report_len_ = 0;  ///< bytes requested per poll
};

}  // namespace cinux::drivers
