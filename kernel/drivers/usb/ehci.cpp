/**
 * @file    ehci.cpp
 * @brief   The EHCI bring-up: capability walk, reset, port presence.
 *
 * The first armv7 USB slice. Everything here is the standard EHCI
 * register vocabulary: CAPLENGTH and friends in the capability
 * window, USBCMD/USBSTS in the operational block, PORTSC per root
 * port. The chipidea wrapper on i.MX6 silicon answers this dialect
 * in host mode; its own registers are not touched at all.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.1
 * @since   0.9.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#include "kernel/drivers/usb/ehci.hpp"

#include <stdint.h>

#include "cinux/bit_ops/bitmask.hpp"
#include "kernel/arch/instructions.hpp"

namespace cinux::driver::usb {

namespace {

/// @brief Capability window: where it sits inside the block.
constexpr unsigned long kCapsOffset = 0x100;

/// @brief Capability register: the operational block's offset, one byte.
constexpr unsigned long kRegCapLength = 0x00;

/// @brief Capability register: the interface revision, two bytes.
constexpr unsigned long kRegHciVersion = 0x02;

/// @brief Capability register: the port count's nibble.
constexpr unsigned long kRegHcsParams = 0x04;

/// @brief HCSPARAMS field: how many root ports this controller owns.
constexpr cinux::base::bit::BitRange kPortsRange{.low = 0, .width = 4};

/// @brief Operational register: run and reset bits.
constexpr unsigned long kRegUsbCommand = 0x00;

/// @brief Operational register: the halted bit lives here.
constexpr unsigned long kRegUsbStatus = 0x08;

/// @brief Operational register: the routed-ports claim, all at once.
constexpr unsigned long kRegConfigFlag = 0x40;

/// @brief Operational register: the first root port's status.
constexpr unsigned long kRegPortSc = 0x44;

/// @brief PORTSC bit: the port's power switch.
constexpr uint32_t kPortPower = 1U << 12U;

/// @brief USBCMD bit: start the controller.
constexpr uint32_t kRunStop = 1U;

/// @brief USBCMD bit: reset the controller, self-clearing.
constexpr uint32_t kHostReset = 1U << 1U;

/// @brief USBSTS bit: the controller has halted.
constexpr uint32_t kHcHalted = 1U << 12U;

/// @brief PORTSC bit: a device answers on this port right now.
constexpr uint32_t kCurrentConnect = 1U;

/// @brief Bounded spins for a hypervisor that answers in microseconds.
constexpr unsigned long kSpinLimit = 2000000;

/// @brief One bound to refuse port numbers with.
constexpr unsigned int kPortCeiling = 15;

}  // namespace

bool EhciHost::init(uint64_t base) {
    window_ = reinterpret_cast<volatile unsigned char*>(  // NOLINT(performance-no-int-to-ptr)
        static_cast<unsigned long>(base));
    revision_ = static_cast<uint16_t>(read32(kCapsOffset + kRegHciVersion) & 0xFFFFU);
    if (revision_ == 0) {
        window_ = nullptr;
        return false;
    }
    op_base_ = kCapsOffset + (read32(kCapsOffset + kRegCapLength) & 0xFFU);
    ports_   = static_cast<unsigned int>(
        cinux::base::bit::BitMask<uint32_t>{read32(kCapsOffset + kRegHcsParams)}.extract(
            kPortsRange));

    write32(op_base_ + kRegUsbCommand, kHostReset);
    if (!wait_clear(op_base_ + kRegUsbCommand, kHostReset)) {
        window_ = nullptr;
        return false;
    }
    write32(op_base_ + kRegUsbCommand, kRunStop);
    if (!wait_clear(op_base_ + kRegUsbStatus, kHcHalted)) {
        window_ = nullptr;
        return false;
    }
    write32(op_base_ + kRegConfigFlag, 1U);
    for (unsigned int port = 1; port <= ports_ && port <= kPortCeiling; ++port) {
        write32(op_base_ + kRegPortSc + ((port - 1) * 4U), kPortPower);
    }
    SeatPrimaryHost(this);
    return true;
}

uint16_t EhciHost::revision() const {
    return revision_;
}

bool EhciHost::present() {
    return window_ != nullptr;
}

unsigned int EhciHost::max_ports() {
    return ports_;
}

bool EhciHost::port_connected(unsigned int port) {
    return port >= 1 && port <= ports_ &&
           (read32(op_base_ + kRegPortSc + ((port - 1) * 4U)) & (kPortPower | kCurrentConnect)) ==
               (kPortPower | kCurrentConnect);
}

bool EhciHost::port_taken(unsigned int /*port*/) {
    return false;
}

void* EhciHost::address(unsigned int /*port*/) {
    return nullptr;
}

bool EhciHost::control_read(void* /*seat*/, uint16_t /*value*/, void* /*data*/,
                            uint16_t /*length*/) {
    return false;
}

bool EhciHost::set_configuration(void* /*seat*/, uint8_t /*config_value*/) {
    return false;
}

bool EhciHost::configure_interrupt_in(void* /*seat*/, uint16_t /*max_packet*/,
                                       uint8_t /*interval*/) {
    return false;
}

void EhciHost::arm_interrupt_in(void* /*seat*/, uint64_t /*buffer_physical*/,
                                unsigned long /*length*/,
                                void (*/*done*/)(void*, const TransferDone&),
                                void* /*owner*/) {}

void EhciHost::detach(void* /*seat*/) {}

void EhciHost::pump() {}

bool EhciHost::unhealthy() {
    return unhealthy_;
}

uint32_t EhciHost::read32(unsigned long offset) const {
    return *reinterpret_cast<const volatile uint32_t*>(window_ + offset);
}

void EhciHost::write32(unsigned long offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(window_ + offset) = value;
}

bool EhciHost::wait_clear(unsigned long offset, uint32_t mask) {
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        if ((read32(offset) & mask) == 0) {
            return true;
        }
        arch::CpuRelax();
    }
    return false;
}

}  // namespace cinux::driver::usb
