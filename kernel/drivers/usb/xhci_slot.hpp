/**
 * @file kernel/drivers/usb/xhci_slot.hpp
 * @brief Per-device xHCI slot: device/input contexts + EP0 control ring
 *
 * Owns the DMA-backed contexts for one USB device and builds the input context
 * for the Address Device command (Batch 3B), consuming the 3A context encoders.
 * The device context is registered in the DCBAA by the caller (controller) via
 * device_context_phys(); the input context is fed to the Address Device command
 * via input_context_phys().  Control transfers (GET_DESCRIPTOR /
 * SET_CONFIGURATION) on the EP0 ring land in Batch 3C.
 *
 * Namespace: cinux::drivers::usb
 */

#pragma once

#include <stdint.h>

#include <cinux/expected.hpp>

#include "kernel/drivers/dma/dma_buffer.hpp"
#include "xhci_context.hpp"
#include "xhci_ring.hpp"

namespace cinux::drivers::usb {

class XhciSlot {
public:
    /// Allocate the device context (slot + EP0 = 64 B), input context (ICC +
    /// device-context copy = 96 B), and the EP0 control transfer ring.  Does NOT
    /// touch the DCBAA -- the caller registers device_context_phys() via
    /// XHCIController::dcbaa_set(slot_id, ...).  Error::OutOfMemory on alloc fail.
    cinux::lib::ErrorOr<void> allocate(uint8_t slot_id);

    /// Fill the input context for Address Device (add slot + EP0):
    ///   - Input Control Context: A0 | A1 (add slot, add EP0)
    ///   - Slot Context: route=0, speed, root-hub port, last_ctx=1 (EP0 only)
    ///   - EP0 Context: control EP, @p ep0_max_packet, EP0-ring dequeue + DCS
    /// @p ep0_max_packet: 8 for FS/LS (unknown until descriptor read), 64 for HS.
    void build_address_input(uint32_t speed, uint8_t rh_port, uint32_t ep0_max_packet);

    uint8_t  slot_id() const { return slot_id_; }
    uint64_t device_context_phys() const { return dev_ctx_buf_.phys(); }
    uint64_t input_context_phys() const { return in_ctx_buf_.phys(); }

    /// Device-context slot dword @p i (0..7) -- read the controller-written
    /// state after Address Device (DW3 dev_state [31:27] = slot state).
    uint32_t device_slot_dword(uint32_t i) const {
        return static_cast<volatile uint32_t*>(dev_ctx_buf_.virt())[i];
    }

    /// Input-context dword @p i (0..23: ICC 0..7, slot 8..15, EP0 16..23).
    uint32_t input_dword(uint32_t i) const {
        return static_cast<volatile uint32_t*>(in_ctx_buf_.virt())[i];
    }

    TrbRing& ep0_ring() { return ep0_ring_; }

private:
    uint8_t                        slot_id_ = 0;
    cinux::drivers::dma::DmaBuffer dev_ctx_buf_;   ///< slot + EP0 device context (output)
    cinux::drivers::dma::DmaBuffer in_ctx_buf_;    ///< ICC + device-context copy (input)
    cinux::drivers::dma::DmaBuffer ep0_ring_buf_;  ///< EP0 transfer ring storage
    TrbRing                        ep0_ring_;
};

}  // namespace cinux::drivers::usb
