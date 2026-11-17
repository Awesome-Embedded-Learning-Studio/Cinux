/**
 * @file    ehci.hpp
 * @brief   The EHCI host controller: bring-up and port presence.
 *
 * One USB host controller face behind the standard EHCI register
 * vocabulary — the shape the i.MX6 UL's chipidea blocks answer to in
 * host mode, and any PCI-less lane this tree grows. This first slice
 * owns the capability walk, the reset dance, and root-port presence;
 * transfers land here as the armv7 USB lane grows.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.1
 * @since   0.9.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "kernel/drivers/usb/hcd.hpp"

namespace cinux::driver::usb {

/**
 * @brief   The EHCI controller this lane carries.
 * @note    Register state only: the capability window's shape and the
 *          operational words. Each controller has its own instance,
 *          initialized before execution and kept for the whole boot.
 * @since   0.9.0
 * @ingroup kernel_driver
 */
class EhciHost : public HostDriver {
public:
    EhciHost()                           = default;
    EhciHost(const EhciHost&)            = delete;
    EhciHost& operator=(const EhciHost&) = delete;

    /**
     * @brief         Walks the capability window and starts the controller.
     *
     * @param[in]     base   The controller's first bus address.
     * @return        True when the controller runs; false when the
     *                window stayed silent or the reset stalled.
     * @since         0.9.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] bool init(uint64_t base);

    /// The interface revision the capability window stated, 0 before init.
    [[nodiscard]] uint16_t revision() const;

    // HostDriver overrides. The transfer faces stay dark this slice.
    [[nodiscard]] bool present() override;
    [[nodiscard]] unsigned int max_ports() override;
    [[nodiscard]] bool port_connected(unsigned int port) override;
    [[nodiscard]] bool port_taken(unsigned int port) override;
    [[nodiscard]] void* address(unsigned int port) override;
    [[nodiscard]] bool control_read(void* seat, uint16_t value, void* data,
                                    uint16_t length) override;
    [[nodiscard]] bool set_configuration(void* seat, uint8_t config_value) override;
    [[nodiscard]] bool configure_interrupt_in(void* seat, uint16_t max_packet,
                                              uint8_t interval) override;
    void arm_interrupt_in(void* seat, uint64_t buffer_physical, unsigned long length,
                          void (*done)(void* owner, const TransferDone&),
                          void* owner) override;
    /**
     * @brief Frees the controller state for one device.
     * @param[in] seat Device handle returned by address().
     */
    void detach(void* seat) override;
    /// @brief Drains completed interrupt transfers and invokes their callbacks.
    void pump() override;
    [[nodiscard]] bool unhealthy() override;

private:
    /// @brief One seated device's state, one page of hardware words each.
    struct Seat {
        unsigned int  port      = 0;
        unsigned int  address   = 0;
        unsigned long page_phys = 0;
        bool          used      = false;

        // The interrupt-in street: one QH and qTD live later in the same
        // page, one armed report at a time.
        bool          intr_live   = false;
        bool          intr_toggle = false;
        bool          intr_linked = false;
        void        (*done)(void* owner, const TransferDone& finished) = nullptr;
        void*         owner         = nullptr;
        unsigned long report_target = 0;
        unsigned long report_bytes  = 0;
    };

    [[nodiscard]] uint32_t read32(unsigned long offset) const;
    void                   write32(unsigned long offset, uint32_t value);
    [[nodiscard]] bool     wait_clear(unsigned long offset, uint32_t mask);
    [[nodiscard]] bool     ring_doorbell();
    [[nodiscard]] bool     stop_async();
    [[nodiscard]] bool     queue_control(Seat& seat, bool in, uint8_t request,
                                         uint16_t value, uint16_t index, void* data,
                                         uint16_t length);
    [[nodiscard]] bool     port_reset(unsigned int port);

public:
    /// Debug: 0 ok, 4 reset, 5 schedule start, 6 timeout, 7 transfer,
    /// 8 doorbell, 9 schedule stop, 10 unsupported length/direction.
    [[nodiscard]] unsigned int last_fail() const { return last_fail_; }

private:

    static constexpr unsigned int kSeats = 4;

    /// Rebuilds the periodic chain across every linked seat and points
    /// the frame list at its head, enabling or disabling the schedule.
    void relink_periodic();

    volatile unsigned char* window_    = nullptr;
    unsigned long           op_base_   = 0;
    unsigned int            ports_     = 0;
    uint16_t                revision_  = 0;
    bool                    unhealthy_ = false;
    Seat                    seats_[kSeats] = {};
    unsigned long           pfl_phys_ = 0;
    unsigned int            next_address_ = 1;
    unsigned int            last_fail_ = 0;
};

}  // namespace cinux::driver::usb
