/**
 * @file    usb_device.hpp
 * @brief   One seated USB device, typed on the class driver it serves.
 *
 * The middle layer of the USB stack. The host controller below seals
 * the neutral HostDriver face; the class driver above holds one
 * UsbDevice and speaks transfers to it. A device owns its report
 * page and one armed AsyncCallback per outstanding interrupt-in —
 * completions arrive from the host's pump and fire once, straight
 * into the class driver's own type.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.3
 * @since   0.6.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <stdint.h>

#include "cinux/async.hpp"
#include "kernel/drivers/usb/hcd.hpp"
#include "kernel/drivers/usb/xhci_layout.hpp"

namespace cinux::driver::usb {

/**
 * @brief   One addressed USB device behind one host controller.
 * @note    The class driver's type rides along: the report callback is
 *          typed on it, so no untyped word crosses this layer. One
 *          interrupt-in endpoint rides along too, the shape every
 *          boot device this station carries needs.
 * @since   0.6.0
 * @ingroup kernel_driver
 */
template <typename Owner>
class UsbDevice {
public:
    /// What the class driver hands over for report completions.
    using ReportHandler = base::AsyncCallback<Owner>::Handler;

    /**
     * @brief         Walks free ports until one device answers the protocol.
     *
     * @param[in,out] host       The controller that owns the ports.
     * @param[in]     protocol   The HID interface protocol wanted, a
     *                           kHidProtocol word.
     * @param[out]    endpoint   The answering device's endpoint facts.
     * @return        True when one device kept its seat for the caller.
     * @note          Ports a seated device holds are skipped, and a
     *                device that answers with the wrong protocol is
     *                detached so the walk can try the next port.
     * @since         0.9.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] bool enumerate(HostDriver& host, uint8_t protocol, KeyboardEp& endpoint);

    /**
     * @brief         Returns the seat and frees the device's report page.
     *
     * @param[in,out] host   The controller that owns the seat.
     * @return        True when the seat left this device's hands, also
     *                when nothing was seated.
     * @since         0.9.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] bool detach(HostDriver& host);

    /**
     * @brief         One SET_CONFIGURATION over endpoint zero.
     *
     * @param[in,out] host           The controller that owns the seat.
     * @param[in]     config_value   The configuration the descriptors offered.
     * @return        True when the device took the configuration.
     * @since         0.6.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] bool set_configuration(HostDriver& host, uint8_t config_value);

    /**
     * @brief         Brings the device's interrupt-in endpoint to life.
     *
     * @param[in,out] host         The controller that owns the seat.
     * @param[in]     max_packet   The endpoint's packet ceiling.
     * @param[in]     interval     The endpoint's polling interval.
     * @return        True when the controller armed the endpoint.
     * @since         0.6.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] bool configure_interrupt_in(HostDriver& host, uint16_t max_packet,
                                              uint8_t interval);

    /**
     * @brief         Arms one interrupt-in read into the report page.
     *
     * @param[in,out] host      The controller that owns the seat.
     * @param[in]     handler   Fires once per finished report.
     * @param[in,out] owner     The handler's own type.
     * @note          The first call takes the report page; every call
     *                re-arms. The handler's status word is zero when
     *                the transfer finished cleanly.
     * @since         0.6.0
     * @ingroup       kernel_driver
     */
    void interrupt_read(HostDriver& host, ReportHandler handler, Owner* owner);

    /**
     * @brief   The report page's kernel view, valid inside a handler.
     * @since   0.6.0
     * @ingroup kernel_driver
     */
    [[nodiscard]] const unsigned char* report() const;

    /// Whether a device holds this seat.
    [[nodiscard]] bool seated() const;

private:
    static void trampoline(void* device, const TransferDone& done);

    void*                    seat_         = nullptr;
    uint64_t                 report_page_  = 0;
    base::AsyncCallback<Owner> report_call_{};
};

}  // namespace cinux::driver::usb
