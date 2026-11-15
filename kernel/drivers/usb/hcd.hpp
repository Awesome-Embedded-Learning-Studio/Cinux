/**
 * @file    hcd.hpp
 * @brief   The neutral host-controller face every USB stack lane speaks.
 *
 * The seam between the USB protocol and the silicon below it. The
 * device layer and the class drivers speak only this vocabulary:
 * ports, one seated device handle, control transfers, one armed
 * interrupt-in completion. Slots, rings, contexts, doorbells and
 * completion-code dialects stay inside the controller implementation
 * that seals this face — xHCI today, chipidea when the armv7 lane
 * grows its host.
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

namespace cinux::driver::usb {

/**
 * @brief   One finished transfer as the device layer reads it.
 * @note    The controller translates its own completion dialect into
 *          this shape: ok covers clean finishes and short packets,
 *          the length is what actually crossed the wire.
 * @since   0.9.0
 * @ingroup kernel_driver
 */
struct TransferDone {
    bool          ok;      ///< True when the transfer finished cleanly.
    unsigned long length;  ///< Bytes the transfer carried.
};

/**
 * @brief   The host controller face the USB stack speaks to.
 * @note    Pure contract. One implementation per controller; the
 *          controllers register at bring-up so class drivers can walk
 *          devices on each bus.
 *          A null return from address() promises nothing stayed
 *          seated on that port.
 * @since   0.9.0
 * @ingroup kernel_driver
 */
class HostDriver {
public:
    /// Whether a controller was brought up this boot.
    [[nodiscard]] virtual bool present() = 0;

    /// Root-hub ports the controller offers.
    [[nodiscard]] virtual unsigned int max_ports() = 0;

    /// Whether a device answers on that root-hub port.
    [[nodiscard]] virtual bool port_connected(unsigned int port) = 0;

    /// Whether a seated device already holds that port.
    [[nodiscard]] virtual bool port_taken(unsigned int port) = 0;

    /**
     * @brief         Seats and addresses the device on one port.
     *
     * @param[in]     port   Root-hub port number, one-based.
     * @return        The seat handle, null when the port refused.
     * @note          The whole v1 sequence: reset, context build,
     *                address. On null the controller cleaned up after
     *                itself and the port is free again.
     */
    [[nodiscard]] virtual void* address(unsigned int port) = 0;

    /// One control read over endpoint zero; descriptor vocabulary.
    [[nodiscard]] virtual bool control_read(void* seat, uint16_t value, void* data,
                                            uint16_t length) = 0;

    /// One SET_CONFIGURATION over endpoint zero.
    [[nodiscard]] virtual bool set_configuration(void* seat, uint8_t config_value) = 0;

    /// Brings the device's interrupt-in endpoint to life.
    [[nodiscard]] virtual bool configure_interrupt_in(void* seat, uint16_t max_packet,
                                                      uint8_t interval) = 0;

    /**
     * @brief         Arms one interrupt-in transfer into a DMA-ready page.
     *
     * @param[in]     seat             The seated device.
     * @param[in]     buffer_physical  Bus address completions land in.
     * @param[in]     length           Bytes the page takes.
     * @param[in]     done             Fires once, from the pump below.
     * @param[in]     owner            The done's first word.
     */
    virtual void arm_interrupt_in(void* seat, uint64_t buffer_physical, unsigned long length,
                                  void (*done)(void* owner, const TransferDone&),
                                  void* owner) = 0;

    /// Returns the seat and frees the controller's per-device pages.
    virtual void detach(void* seat) = 0;

    /// @brief Drains finished events and fires armed completions.
    virtual void pump() = 0;

    /// Whether the controller reported a host-level error.
    [[nodiscard]] virtual bool unhealthy() = 0;

protected:
    /// Non-virtual and trivial: seats live for the whole boot and are
    /// never deleted through this face, so the controller's singleton
    /// keeps its zero-destruction shape.
    ~HostDriver() = default;
};

/// The most recently initialized host, retained for bring-up probes.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline HostDriver* g_primary_host = nullptr;

/// Controllers registered this boot, in bring-up order.
inline HostDriver* g_hosts[4] = {};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

/**
 * @brief Returns the registered controllers in bring-up order.
 * @return Host pointers; unused entries are null.
 */
[[nodiscard]] inline auto& Hosts() {
    return g_hosts;
}

/// The host this boot seated, null before any controller came up.
[[nodiscard]] inline HostDriver* PrimaryHost() {
    return g_primary_host;
}

/**
 * @brief Registers a controller and retains it as the primary host.
 * @param[in] host Controller that just completed initialization.
 */
inline void SeatPrimaryHost(HostDriver* host) {
    g_primary_host = host;
    for (HostDriver*& seated : g_hosts) {
        if (seated == host) {
            return;
        }
        if (seated == nullptr) {
            seated = host;
            return;
        }
    }
}

}  // namespace cinux::driver::usb
