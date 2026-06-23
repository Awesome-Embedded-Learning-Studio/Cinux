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

#include "kernel/drivers/dma/dma_buffer.hpp"
#include "kernel/drivers/pci/msix.hpp"
#include "kernel/drivers/pci/pci.hpp"
#include "xhci_irq.hpp"
#include "xhci_registers.hpp"
#include "xhci_ring.hpp"

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

    /**
     * @brief Allocate the rings + DCBAA and run the controller
     *
     * Allocates (DmaPool) and programs: DCBAA (+ scratchpad if HCSPARAMS2
     * requests it), the command ring (CRCR), the event ring + a one-segment
     * ERST, IR0 (enable + moderation), CONFIG.MaxSlotsEn, then sets
     * USBCMD.RS+INTE and waits for the controller to leave HCH (running).
     * Leaves the controller running, ready to receive doorbells (Batch 2C).
     */
    cinux::lib::ErrorOr<void> start();

    TrbRing&   command_ring() { return cmd_ring_; }
    EventRing& event_ring() { return event_ring_; }

    /// Enqueue a command TRB on the command ring and ring doorbell 0.
    void submit_command(uint64_t parameter, uint32_t status, uint32_t control);

    /// Dequeue pending event TRBs, advance ERDP, and clear IMAN.IP.  Called
    /// from the ISR hook (event_irq_thunk) in production and polled directly
    /// by tests (the test kernel does not enable CPU interrupts).
    void poll_events();

    /// Count of Command Completion Events observed by poll_events().
    uint32_t cmd_completions() const { return cmd_completion_count_; }

    /// Synchronous command: enqueue + doorbell, then poll until THIS command's
    /// Command Completion Event arrives (matched by the command-TRB pointer the
    /// controller echoes back), returning the completion event.  Times out ->
    /// Error::TimedOut.  Caller reads slot_id / completion_code from the result.
    cinux::lib::ErrorOr<Trb> run_command(uint64_t parameter, uint32_t status, uint32_t control);

    /// Most recent Command Completion Event captured by poll_events().
    Trb last_cmd_completion() const { return last_cmd_completion_; }

    // ---- Port + slot management (Batch 3B: Address Device) ----

    /// PORTSC register for @p port (op_base + 0x400 + port*0x10).
    volatile uint32_t*            portsc(uint8_t port);
    uint32_t                      read_portsc(uint8_t port);
    /// Assert port reset, wait for PORT_RESET self-clear, clear change bits,
    /// return the PORTSC device speed (0=undef,1=FS,2=LS,3=HS,4=SS).
    /// Error::TimedOut if reset never completes.
    cinux::lib::ErrorOr<uint32_t> port_reset(uint8_t port);
    /// Write a device-context physical address into DCBAA[slot].
    void                          dcbaa_set(uint8_t slot, uint64_t phys);

    /// ISR hook target: dispatches to s_instance_->poll_events().
    static void event_irq_thunk();

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

    pci::PCIDevice            dev_{};
    pci::msix::MsixCap        msix_cap_{};
    pci::msix::MsixController msix_;
    uint32_t                  cmd_completion_count_ = 0;
    Trb                       last_cmd_completion_{};

    // DMA-backed rings + tables (own physical memory for the controller's
    // lifetime).  DmaBuffer is move-only, so XHCIController is move-only.
    cinux::drivers::dma::DmaBuffer dcbaa_buf_;
    cinux::drivers::dma::DmaBuffer scratchpad_arr_buf_;
    cinux::drivers::dma::DmaBuffer scratchpad_pages_buf_;
    cinux::drivers::dma::DmaBuffer cmd_ring_buf_;
    cinux::drivers::dma::DmaBuffer event_ring_buf_;
    cinux::drivers::dma::DmaBuffer erst_buf_;
    TrbRing                        cmd_ring_;
    EventRing                      event_ring_;

    static XHCIController* s_instance_;
};

}  // namespace cinux::drivers::usb
