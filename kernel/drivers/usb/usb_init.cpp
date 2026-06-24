/**
 * @file kernel/drivers/usb/usb_init.cpp
 * @brief Boot USB bring-up: discover xHCI, enumerate the HID boot mouse, arm async input
 *
 * Batch 5A.  The enumeration sequence mirrors kernel/test/test_xhci.cpp's
 * test_hid_mouse (port scan -> reset -> Enable Slot -> Address Device ->
 * GET_DESCRIPTOR(Configuration) -> find_boot_mouse -> Configure Endpoint),
 * but targets the production async input path: after SET_PROTOCOL + Configure
 * Endpoint the mouse registers as a TransferListener and submits its first
 * async interrupt-IN transfer.  From then on input is interrupt-driven.
 *
 * Namespace: cinux::drivers::usb
 */

#include "usb_init.hpp"

#include <stdint.h>

#include "kernel/drivers/mouse/mouse.hpp"
#include "kernel/drivers/mouse/usb_mouse.hpp"
#include "kernel/drivers/pci/pci.hpp"
#include "kernel/drivers/usb/usb_descriptor.hpp"
#include "kernel/drivers/usb/xhci_controller.hpp"
#include "kernel/drivers/usb/xhci_slot.hpp"
#include "kernel/lib/kprintf.hpp"

namespace cinux::drivers::usb {

// Persistent storage for the controller + the enumerated mouse.  These must
// outlive boot: the controller's rings/contexts and the slot's DMA buffers stay
// live for the system lifetime, and the mouse is a TransferListener the ISR
// calls on every report.  (Batch 5A wires the mouse only; a keyboard + its own
// slot arrive in 5B, reusing the same async + listener mechanism.)
namespace {
XHCIController           g_xhci;
XhciSlot                 g_mouse_slot;
cinux::drivers::UsbMouse g_mouse;
}  // namespace

void init() {
    pci::PCI       pci;
    pci::PCIDevice dev{};
    if (!pci.find_xhci(dev)) {
        cinux::lib::kprintf("[xHCI] no controller present -- USB input disabled\n");
        return;  // default QEMU (run-kernel-test) has no qemu-xhci: graceful
    }

    if (!g_xhci.init(dev).ok() || !g_xhci.start().ok()) {
        cinux::lib::kprintf("[xHCI] controller bring-up failed -- USB input disabled\n");
        return;
    }
    XHCIController::set_instance(&g_xhci);

    // Scan ports; enumerate the first HID boot mouse.
    for (uint8_t port = 0; port < g_xhci.max_ports(); ++port) {
        if (!(g_xhci.read_portsc(port) & Portsc::kCurrentConnect)) {
            continue;
        }
        auto speed_r = g_xhci.port_reset(port);
        if (!speed_r.ok()) {
            continue;
        }
        auto es = g_xhci.run_command(0, 0, trb_control(TrbType::kEnableSlot));
        if (!es.ok()) {
            continue;
        }
        const uint8_t slot_id = static_cast<uint8_t>(cmd_completion_slot_id(es.value().control));
        if (!g_mouse_slot.allocate(slot_id).ok()) {
            continue;
        }
        g_xhci.dcbaa_set(slot_id, g_mouse_slot.device_context_phys());

        const uint32_t speed = speed_r.value();
        const uint32_t maxp  = (speed == UsbSpeed::kHigh) ? 64 : 8;
        g_mouse_slot.build_address_input(speed, port + 1, maxp);
        auto ad =
            g_xhci.run_command(g_mouse_slot.input_context_phys(), 0,
                               trb_control(TrbType::kAddressDevice) | slot_id_for_trb(slot_id));
        if (!ad.ok() || cmd_completion_code(ad.value().status) != CompCode::kSuccess) {
            continue;
        }

        auto cfg = g_mouse_slot.get_descriptor(g_xhci, UsbDescType::kConfiguration, 0, 255);
        if (!cfg.ok()) {
            continue;
        }
        BootMouseEp mep{};
        if (!find_boot_mouse(g_mouse_slot.data_virt(), cfg.value(), mep)) {
            continue;  // not a boot mouse (e.g. the keyboard) -- try the next port
        }

        if (!g_mouse_slot.set_configuration(g_xhci, 1).ok()) {
            continue;
        }
        g_mouse.bind(g_mouse_slot);
        // Register the listener BEFORE the first async transfer, so a report
        // arriving the instant the transfer completes still dispatches.
        g_xhci.register_transfer_listener(slot_id, &g_mouse);
        if (!g_mouse.init(g_xhci, mep).ok()) {
            cinux::lib::kprintf("[xHCI] HID boot mouse init failed (slot=%u)\n", slot_id);
            continue;
        }
        g_mouse.arm();  // submit the first async IN transfer (listener already registered)
        cinux::drivers::Mouse::set_usb_primary(true);  // PS/2 stays as silent fallback
        cinux::lib::kprintf("[xHCI] HID boot mouse armed: slot=%u ep%u-IN (async interrupt-IN)\n",
                            slot_id, mep.ep_number);
        return;  // mouse armed
    }

    cinux::lib::kprintf("[xHCI] no HID boot mouse found -- USB mouse disabled\n");
}

}  // namespace cinux::drivers::usb
