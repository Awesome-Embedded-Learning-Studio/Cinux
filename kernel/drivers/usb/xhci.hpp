/**
 * @file    xhci.hpp
 * @brief   The xHCI host controller: bring-up, rings, and the drain.
 *
 * Meyers-singleton shape after Ahci. This layer owns the controller
 * and nothing above it: the register window, the reset dance, the
 * command and event rings, and the delivery of completions — polled by
 * readers, and armed through MSI-X for the day real interrupts arrive,
 * the v1 boundary kept honestly: under this hypervisor delivery is
 * unreliable and the drain does the carrying. Seated devices live one
 * layer up in usb_device and receive their transfer events here.
 *
 * @author  Charliechen114514
 * @date    2026-10-22
 * @version 0.2
 * @since   0.6.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "cinux/singleton.hpp"
#include "cinux/spin_mutex.hpp"
#include "kernel/drivers/usb/hcd.hpp"
#include "kernel/drivers/usb/usb_config.hpp"
#include "kernel/drivers/usb/xhci_layout.hpp"

namespace cinux::driver::usb {

template <typename Owner>
class UsbDevice;

/**
 * @brief   One seated device's xHCI state, all of it silicon vocabulary.
 * @note    Slots, context pages, endpoint rings and doorbell targets
 *          live here and never leave this driver; the neutral face
 *          above only ever sees the seat pointer.
 * @since   0.9.0
 * @ingroup kernel_driver
 */
struct XhciSeat {
    /// The device's slot number, zero when the seat is free.
    uint8_t slot = 0;
    /// The attached device's speed code.
    uint8_t speed = 0;
    /// The root-hub port the device answered on, zero when free.
    uint8_t port = 0;
    /// The slot's input context page, bus address.
    uint64_t input_page = 0;
    /// The slot's output context page, bus address.
    uint64_t output_page = 0;
    /// The control endpoint's ring page, bus address.
    uint64_t ep0_page = 0;
    /// The interrupt endpoint's ring page, bus address.
    uint64_t ep1_page = 0;
    /// The control endpoint's ring cursor.
    RingState ep0_{};
    /// The interrupt endpoint's ring cursor.
    RingState ep1_{};
    /// The transfer event the control spin waits for.
    uint64_t control_trb = 0;
    /// Whether the waited control transfer finished.
    bool control_done = false;
    /// The waited control transfer's completion code.
    uint8_t control_code = 0;
    /// The armed interrupt transfer's ring word.
    uint64_t report_trb = 0;
    /// Whether an interrupt transfer is in flight.
    bool report_live = false;
    /// Bytes the armed interrupt page takes.
    unsigned long report_bytes = 0;
    /// Where the neutral layer wants the finished report, bus address.
    uint64_t report_target = 0;
    /// The armed interrupt's completion, neutral cargo only.
    void (*done)(void* owner, const TransferDone&) = nullptr;
    /// The done's first word.
    void* owner = nullptr;
};

}  // namespace cinux::driver::usb

namespace cinux::driver {

struct PciDevice;

/**
 * @brief   The USB host controller this machine carries.
 * @note    All state is controller state: window, regions, rings and
 *          the seated-device pointers events route to. None of it
 *          changes shape after init, so the zero-construction rule
 *          holds.
 * @since   0.6.0
 * @ingroup kernel_driver
 */
class XhciHost : public usb::HostDriver, public base::Singleton<XhciHost> {
    friend class base::Singleton<XhciHost>;
    template <typename Owner>
    friend class usb::UsbDevice;

public:
    /**
     * @brief         Finds the controller and walks the bring-up sequence.
     *
     * @return        True when the controller runs; false when absent or
     *                stalled, in which case nothing stays mapped.
     * @warning       None
     * @throws        None
     * @since         0.6.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] bool init();

    /**
     * @brief         Whether a controller was brought up.
     *
     * @return        True after a successful init on this boot.
     * @warning       None
     * @throws        None
     * @since         0.6.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] bool present() override;

    /**
     * @brief         Root-hub ports the controller offers.
     *
     * @return        Port count as the capability region stated.
     * @warning       None
     * @throws        None
     * @since         0.6.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] unsigned int max_ports() override;

    /**
     * @brief         Drains every finished event on the ring.
     *
     * @note          Command completions land in the waiter that asked,
     *                transfer events route to the seated device that
     *                owns the TRB. Readers poll this, the armed message
     *                vector calls it, and both carry the same cargo.
     * @warning       None
     * @throws        None
     * @since         0.6.0
     * @ingroup       kernel_driver
     */
    void drain_events();

    /**
     * @brief         Whether the controller reported a host system error.
     *
     * @return        False while the controller is healthy.
     * @warning       None
     * @throws        None
     * @since         0.6.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] bool unhealthy() override;

    /// The neutral faces below the protocol layer, all override.
    [[nodiscard]] bool port_connected(unsigned int port) override;
    [[nodiscard]] bool port_taken(unsigned int port) override;
    [[nodiscard]] void* address(unsigned int port) override;
    [[nodiscard]] bool control_read(void* seat, uint16_t value, void* data,
                                    uint16_t length) override;
    [[nodiscard]] bool set_configuration(void* seat, uint8_t config_value) override;
    [[nodiscard]] bool configure_interrupt_in(void* seat, uint16_t max_packet,
                                              uint8_t interval) override;
    void arm_interrupt_in(void* seat, uint64_t buffer_physical, unsigned long length,
                          void (*done)(void* owner, const usb::TransferDone&),
                          void* owner) override;
    void detach(void* seat) override;
    void pump() override;

private:
    XhciHost() = default;

    [[nodiscard]] uint32_t read32(unsigned long offset) const;
    void                   write32(unsigned long offset, uint32_t value);
    void                   write64(unsigned long offset, uint64_t value);
    [[nodiscard]] bool     wait_set(unsigned long offset, uint32_t mask);
    [[nodiscard]] bool     wait_clear(unsigned long offset, uint32_t mask);
    [[nodiscard]] bool     halt_and_reset();
    [[nodiscard]] bool     arm_rings();
    [[nodiscard]] bool     start_controller();
    [[nodiscard]] bool     run_command(uint64_t parameter, uint32_t control, uint8_t& slot);
    void                   write_doorbell(unsigned int slot, unsigned int target);
    [[nodiscard]] uint32_t port_status(unsigned int port) const;
    [[nodiscard]] bool     reset_port(unsigned int port, uint8_t& speed);
    void                   seat_transfer(usb::XhciSeat& seat, const usb::Trb& event);
    [[nodiscard]] bool     spin_for_control(usb::XhciSeat& seat);
    void                   ring_report(usb::XhciSeat& seat);
    void                   consume_event();
    [[nodiscard]] bool     await_command(uint64_t trb_address, uint8_t& slot, uint8_t& code);
    void                   arm_delivery(const PciDevice* host);

    volatile unsigned char* window_ = nullptr;
    usb::XhciRegions        regions_{};
    usb::XhciShape          shape_{};
    usb::RingState          command_{};
    unsigned long           event_index_                    = 0;
    bool                    event_cycle_                    = true;
    uint64_t                command_page_                   = 0;
    uint64_t                event_page_                     = 0;
    uint64_t                device_page_                    = 0;
    usb::XhciSeat           seats_[usb::kXhciSlotsEnabled]  = {};
    base::SpinMutex         event_gate_{};
    uint64_t                awaited_trb_   = 0;
    bool                    command_ready_ = false;
    uint8_t                 command_slot_  = 0;
    uint8_t                 command_code_  = 0;
};

}  // namespace cinux::driver
