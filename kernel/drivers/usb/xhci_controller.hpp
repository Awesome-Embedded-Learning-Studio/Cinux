/**
 * @file kernel/drivers/usb/xhci_controller.hpp
 * @brief xHCI host-controller driver (instance + singleton)
 *
 * Brings up a single xHCI host controller discovered via PCI: enables Bus
 * Master + Memory Space, maps BAR0, reads the capability registers, halts and
 * resets the controller.  Subsequent batches add rings (2A/B), the MSI-X
 * event-ring interrupt (2C), device enumeration (3A-C) and HID (4A/B).
 *
 * Modelled on AHCI (instance + singleton, init(const PCIDevice&)) -- NOT the
 * all-static Mouse/Keyboard shape, because xHCI carries per-controller mutable
 * state (rings, slot contexts).
 *
 * Namespace: cinux::drivers::usb
 */

#pragma once

#include <stdint.h>

#include <cinux/expected.hpp>

#include "xhci_registers.hpp"

namespace cinux::drivers::pci {
struct PCIDevice;
}  // namespace cinux::drivers::pci

namespace cinux::drivers::usb {

class XHCIController {
public:
    static XHCIController& instance();
    static void            set_instance(XHCIController* c);

    /**
     * @brief Bring up the controller from a PCI descriptor
     *
     * Enables PCI Bus Master + Memory Space, maps BAR0 into the MMIO window,
     * decodes the capability registers, then halts and resets the controller
     * (USBCMD=0 -> wait HCH -> USBCMD=HCRST -> wait CNR clear + HCRST
     * self-clear).  Leaves the controller halted, ready for ring setup (2B).
     *
     * BIOS/SMM legacy handoff is a no-op on QEMU qemu-xhci (boots OS-owned);
     * real hardware needs the USB Legacy Support extended-capability handoff
     * (follow-up).
     */
    cinux::lib::ErrorOr<void> init(const pci::PCIDevice& dev);

    uint8_t max_ports() const { return max_ports_; }
    uint8_t max_slots() const { return max_slots_; }
    bool    present() const { return cap_regs_ != nullptr; }

    // Raw register access for later phases + tests.
    XhciCapRegs*         cap_regs() const { return cap_regs_; }
    XhciOpRegs*          op_regs() const { return op_regs_; }
    XhciInterrupterRegs* ir0() const { return ir0_; }
    volatile uint32_t*   doorbells() const { return doorbells_; }

private:
    XhciCapRegs*         cap_regs_  = nullptr;
    XhciOpRegs*          op_regs_   = nullptr;
    XhciInterrupterRegs* ir0_       = nullptr;
    volatile uint32_t*   doorbells_ = nullptr;
    uint8_t              max_slots_ = 0;
    uint8_t              max_ports_ = 0;

    static XHCIController* s_instance_;
};

}  // namespace cinux::drivers::usb
