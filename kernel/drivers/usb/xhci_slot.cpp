/**
 * @file kernel/drivers/usb/xhci_slot.cpp
 * @brief XhciSlot: allocate DMA contexts + build the Address Device input context
 *
 * Batch 3B.  Consumes the 3A context encoders (slot_dev_info / ep_info2 /
 * ep_dequeue_ptr / input_add_flag) to fill the input context for Address Device.
 * All bit positions verified against Linux drivers/usb/host/xhci.h.
 *
 * Input Context layout (32-byte contexts, HCCPARAMS1 CSZ=0 -- the QEMU case):
 *   DW 0..7  : Input Control Context  (DW0 = add-flags)
 *   DW 8..15 : Slot Context copy      (DW0 dev_info, DW1 dev_info2)
 *   DW16..23 : EP0 Endpoint Context   (DW1 ep_info2, DW2/3 deq, DW4 tx_info)
 *
 * Namespace: cinux::drivers::usb
 */

#include "xhci_slot.hpp"

#include <stdint.h>

#include "kernel/drivers/dma/dma_pool.hpp"
#include "kernel/lib/kprintf.hpp"
#include "xhci_controller.hpp"

namespace cinux::drivers::usb {

namespace {
constexpr uint64_t kDevCtxBytes  = 64;   ///< slot (32) + EP0 (32)
constexpr uint64_t kInCtxBytes   = 96;   ///< ICC (32) + slot (32) + EP0 (32)
constexpr uint64_t kDataBufBytes = 256;  ///< control-IN data (device + config descriptors)
constexpr uint32_t kEp0RingTrbs  = 16;   ///< EP0 control ring depth (+1 Link TRB)
constexpr uint32_t kEp0AvgTrbLen = 8;    ///< average TRB length for control EP

void zero_bytes(void* p, uint64_t bytes) {
    auto* b = static_cast<uint8_t*>(p);
    for (uint64_t i = 0; i < bytes; ++i) {
        b[i] = 0;
    }
}
}  // namespace

cinux::lib::ErrorOr<void> XhciSlot::allocate(uint8_t slot_id) {
    slot_id_ = slot_id;

    auto d = cinux::drivers::dma::g_dma_pool.alloc(kDevCtxBytes);
    if (!d.ok()) {
        return cinux::lib::Error::OutOfMemory;
    }
    dev_ctx_buf_ = std::move(d.value());
    zero_bytes(dev_ctx_buf_.virt(), kDevCtxBytes);

    auto ic = cinux::drivers::dma::g_dma_pool.alloc(kInCtxBytes);
    if (!ic.ok()) {
        return cinux::lib::Error::OutOfMemory;
    }
    in_ctx_buf_ = std::move(ic.value());
    zero_bytes(in_ctx_buf_.virt(), kInCtxBytes);

    auto er = cinux::drivers::dma::g_dma_pool.alloc(static_cast<uint64_t>(kEp0RingTrbs + 1) * 16);
    if (!er.ok()) {
        return cinux::lib::Error::OutOfMemory;
    }
    ep0_ring_buf_ = std::move(er.value());
    zero_bytes(ep0_ring_buf_.virt(), static_cast<uint64_t>(kEp0RingTrbs + 1) * 16);
    ep0_ring_.init(static_cast<Trb*>(ep0_ring_buf_.virt()), kEp0RingTrbs, ep0_ring_buf_.phys());

    auto db = cinux::drivers::dma::g_dma_pool.alloc(kDataBufBytes);
    if (!db.ok()) {
        return cinux::lib::Error::OutOfMemory;
    }
    data_buf_ = std::move(db.value());
    zero_bytes(data_buf_.virt(), kDataBufBytes);

    return {};
}

void XhciSlot::build_address_input(uint32_t speed, uint8_t rh_port, uint32_t ep0_max_packet) {
    auto* in = static_cast<volatile uint32_t*>(in_ctx_buf_.virt());

    // Input Control Context (DW0..7): DW0 = Drop flags (nothing dropped),
    // DW1 = Add flags (A0 = add slot, A1 = add EP0).  Per xHCI spec / Linux
    // xhci_input_ctrl_ctx, Drop precedes Add (verified against QEMU hcd-xhci,
    // which requires ictx DW0==0 && DW1==0x3 for Address Device).
    in[0] = 0;
    in[1] = input_add_flag(0) | input_add_flag(1);

    // Slot Context (DW8..15): route=0, speed, last_ctx=1 (EP0 only), root-hub port.
    in[8] = slot_dev_info(0, speed, 1);
    in[9] = slot_dev_info2(0, rh_port);
    // DW10 tt_info and DW11 dev_state stay 0 (controller assigns the address).

    // EP0 Endpoint Context (DW16..23): control EP, max packet, EP0-ring dequeue.
    in[17]             = ep_info2(EpType::kControl, ep0_max_packet);
    const uint64_t deq = ep_dequeue_ptr(ep0_ring_buf_.phys(), true);
    in[18]             = static_cast<uint32_t>(deq);
    in[19]             = static_cast<uint32_t>(deq >> 32);
    in[20]             = ep_tx_info(kEp0AvgTrbLen);
}

// ============================================================
// Control transfers on EP0 (Batch 3C)
// TRB stage encoders + completion parsing are in xhci_trb.hpp (verified against
// Linux xhci-ring.c + QEMU hcd-xhci.c).  The cycle bit is added by TrbRing on
// enqueue, so the control words here never include it.
// ============================================================

cinux::lib::ErrorOr<uint32_t> XhciSlot::control_in(XHCIController& hc, const UsbSetup& setup,
                                                   uint32_t len) {
    if (len > kDataBufBytes) {
        return cinux::lib::Error::InvalidArgument;
    }
    // 3-stage control IN: SETUP (TRT=IN) -> Data (IN) -> Status (OUT handshake).
    ep0_ring_.enqueue(setup_to_u64(setup), kSetupStageLength, setup_stage_control(Trt::kIn));
    ep0_ring_.enqueue(data_buf_.phys(), len, data_stage_control(true));
    ep0_ring_.enqueue(0, 0, status_stage_control(true));

    auto ev = hc.run_transfer(slot_id_, kEp0Epid);
    if (!ev.ok()) {
        return ev.error();
    }
    const uint32_t code = cmd_completion_code(ev.value().status);
    if (code != CompCode::kSuccess && code != CompCode::kShortPacket) {
        cinux::lib::kprintf("[xHCI] control_in failed: completion code=%u\n", code);
        return cinux::lib::Error::IOError;
    }
    return len - transfer_event_remaining(ev.value().status);  // actual bytes received
}

cinux::lib::ErrorOr<void> XhciSlot::control_out_no_data(XHCIController& hc, const UsbSetup& setup) {
    // 2-stage control OUT (no data): SETUP (TRT=none) -> Status (IN handshake).
    ep0_ring_.enqueue(setup_to_u64(setup), kSetupStageLength, setup_stage_control(Trt::kNone));
    ep0_ring_.enqueue(0, 0, status_stage_control(false));

    auto ev = hc.run_transfer(slot_id_, kEp0Epid);
    if (!ev.ok()) {
        return ev.error();
    }
    const uint32_t code = cmd_completion_code(ev.value().status);
    if (code != CompCode::kSuccess) {
        cinux::lib::kprintf("[xHCI] control_out failed: completion code=%u\n", code);
        return cinux::lib::Error::IOError;
    }
    return {};
}

cinux::lib::ErrorOr<uint32_t> XhciSlot::get_descriptor(XHCIController& hc, uint8_t desc_type,
                                                       uint16_t index, uint32_t len) {
    return control_in(hc, make_get_descriptor_setup(desc_type, index, len), len);
}

cinux::lib::ErrorOr<void> XhciSlot::set_configuration(XHCIController& hc, uint8_t config_value) {
    const UsbSetup s =
        make_setup(bm_request_type(UsbDir::kOut, UsbReqType::kStandard, UsbRecipient::kDevice),
                   UsbRequest::kSetConfiguration, config_value, 0, 0);
    return control_out_no_data(hc, s);
}

}  // namespace cinux::drivers::usb
