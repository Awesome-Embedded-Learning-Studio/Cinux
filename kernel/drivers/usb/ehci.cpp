/**
 * @file    ehci.cpp
 * @brief   The EHCI bring-up and the async control street.
 *
 * The armv7 USB lane's controller. Everything here is the standard
 * EHCI register vocabulary: the capability walk, the reset dance,
 * port power and reset, and the asynchronous QH/qTD street that
 * carries a control transfer end to end. The chipidea wrapper on
 * i.MX6 silicon answers this dialect in host mode; its own registers
 * are not touched at all.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.2
 * @since   0.9.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#include "kernel/drivers/usb/ehci.hpp"

#include <stdint.h>

#include "cinux/bit_ops/bitmask.hpp"
#include "cinux/memory.hpp"
#include "kernel/arch/instructions.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/pmm.hpp"

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
constexpr unsigned long kRegUsbStatus = 0x04;

/// @brief Operational register: the asynchronous list's first QH.
constexpr unsigned long kRegAsyncList = 0x18;

/// @brief Operational register: the interrupt-enable mask.
constexpr unsigned long kRegIntrEnable = 0x08;

/// @brief Operational register: the periodic frame list's page.
constexpr unsigned long kRegPeriodicList = 0x14;

/// @brief Operational register: the routed-ports claim, all at once.
constexpr unsigned long kRegConfigFlag = 0x40;

/// @brief Operational register: the first root port's status.
constexpr unsigned long kRegPortSc = 0x44;

/// @brief USBCMD bit: start the controller.
constexpr uint32_t kRunStop = 1U;

/// @brief USBCMD bit: reset the controller, self-clearing.
constexpr uint32_t kHostReset = 1U << 1U;

/// @brief USBCMD bit: run the asynchronous schedule.
constexpr uint32_t kAsyncEnable = 1U << 5U;

/// @brief USBCMD bit: run the periodic schedule.
constexpr uint32_t kPeriodicEnable = 1U << 4U;

/// @brief USBCMD bit: the doorbell that ends an asynchronous round.
constexpr uint32_t kAsyncDoorbell = 1U << 6U;

/// @brief USBSTS bit: the controller has halted.
constexpr uint32_t kHcHalted = 1U << 12U;

/// @brief USBSTS bit: the doorbell answered, write one to clear.
constexpr uint32_t kAsyncAdvance = 1U << 5U;

/// @brief USBSTS bit: the asynchronous schedule is running.
constexpr uint32_t kAsyncStatus = 1U << 15U;

/// @brief USBSTS bit: the periodic schedule is running.
constexpr uint32_t kPeriodicStatus = 1U << 14U;

/// @brief PORTSC bit: a device answers on this port right now.
constexpr uint32_t kCurrentConnect = 1U;

/// @brief PORTSC bit: the port's power switch.
constexpr uint32_t kPortPower = 1U << 12U;

/// @brief PORTSC bit: the bus reset a seated device wants, self-clearing.
constexpr uint32_t kPortReset = 1U << 8U;

/// @brief PORTSC bit: the port carries a talking device.
constexpr uint32_t kPortEnabled = 1U << 2U;

/// @brief Bounded spins for a hypervisor that answers in microseconds.
constexpr unsigned long kSpinLimit = 20000000;

/// @brief One bound to refuse port numbers with.
constexpr unsigned int kPortCeiling = 15;

/// @brief Seat page layout: the QH, three qTDs, then the data buffer.
constexpr unsigned long kQhOffset        = 0x00;
constexpr unsigned long kQtdSetup        = 0x40;
constexpr unsigned long kQtdData         = 0x60;
constexpr unsigned long kQtdStatus       = 0x80;
constexpr unsigned long kControlBuffer   = 0x800;
constexpr unsigned long kQhIntr          = 0x100;
constexpr unsigned long kQtdIntr         = 0x140;
constexpr unsigned long kFrameEntries    = 1024;
constexpr unsigned long kControlCapacity = arch::page::kSize - kControlBuffer;

/// @brief qTD token: the transfer's life story in one word.
constexpr uint32_t kTokenActive  = 1U << 7U;
constexpr uint32_t kTokenHalted  = 1U << 6U;
constexpr uint32_t kTokenDataBuf = 1U << 5U;
constexpr uint32_t kTokenBabble  = 1U << 4U;
constexpr uint32_t kTokenXactErr = 1U << 3U;
constexpr uint32_t kTokenMissed  = 1U << 2U;
constexpr uint32_t kTokenSplit   = 1U << 1U;
constexpr uint32_t kTokenPidIn   = 1U;
constexpr uint32_t kTokenPidOut  = 0U;
constexpr uint32_t kTokenPidSetup = 2U;

/// @brief qTD token fields: PID, byte count, interrupt-on-complete.
constexpr uint32_t kPidShift      = 8U;
constexpr uint32_t kBytesShift    = 16U;
constexpr uint32_t kOnComplete    = 1U << 15U;
constexpr uint32_t kDataToggleOne = 1U << 31U;

/// @brief qTD and QH link words end with the terminate bit.
constexpr uint32_t kLinkTerminated = 1U;

/// @brief A list entry's low bits mark the entry a queue head.
constexpr uint32_t kLinkQueueHead = 1U << 1U;

/// @brief QH word one: the endpoint's shape.  Bits follow the EHCI
/// queue-head layout: max packet at sixteen, head flag at fifteen, data
/// toggle control at fourteen, endpoint speed at twelve, endpoint number
/// at eight, device address in the low seven.
constexpr uint32_t kQhMpsShift          = 16U;
constexpr uint32_t kQhHeadOfList        = 1U << 15U;
constexpr uint32_t kQhDataToggleControl = 1U << 14U;
constexpr uint32_t kQhSpeedHigh         = 2U << 12U;
constexpr uint32_t kQhEndpointShift     = 8U;

/// @brief A control endpoint zero's number lives in its QH word one.
constexpr uint32_t kEndpointZero = 0U;

/// @brief Control traffic on a high-speed bus moves in 64-byte packets.
constexpr uint32_t kEndpointZeroMps = 64U;

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

bool EhciHost::port_taken(unsigned int port) {
    for (const Seat& seat : seats_) {
        if (seat.used && seat.port == port) {
            return true;
        }
    }
    return false;
}

void* EhciHost::address(unsigned int port) {
    if (port == 0 || port > ports_ || port > kPortCeiling || !port_connected(port) ||
        port_taken(port)) {
        return nullptr;
    }
    Seat* seat = nullptr;
    for (Seat& candidate : seats_) {
        if (!candidate.used) {
            seat = &candidate;
            break;
        }
    }
    if (seat == nullptr || next_address_ > 127) {
        return nullptr;
    }
    const base::PhysAddr kPage = mm::Pmm::self().allocate_page();
    if (kPage == base::PhysAddr{}) {
        return nullptr;
    }
    seat->used      = true;
    seat->port      = port;
    seat->address   = 0;
    seat->page_phys = kPage.raw;
    last_fail_ = 0;
    if (!port_reset(port)) {
        detach(seat);
        return nullptr;
    }

    // Endpoint zero is usable only after the port has been reset/enabled.
    if (!queue_control(*seat, true, 6 /* GET_DESCRIPTOR */, 0x0100, 0,
                       nullptr, 8)) {
        cinux::print::Println("[ehci] pre-set-descriptor refused %u", last_fail_);
        detach(seat);
        return nullptr;
    }

    // The device answers at zero until SET_ADDRESS lands; the seat's QH
    // starts its life talking to endpoint zero at bus address zero.
    if (!queue_control(*seat, false, 5 /* SET_ADDRESS */,
                       static_cast<uint16_t>(next_address_), 0, nullptr, 0)) {
        if (last_fail_ == 0) {
            last_fail_ = 2;
        }
        detach(seat);
        return nullptr;
    }
    seat->address = next_address_++;
    return seat;
}

bool EhciHost::control_read(void* seat_handle, uint16_t value, void* data, uint16_t length) {
    auto* const kSeat = static_cast<Seat*>(seat_handle);
    if (kSeat == nullptr || !kSeat->used || kSeat->address == 0) {
        return false;
    }
    // The neutral walk only ever asks for descriptors: IN, standard,
    // deviceRecipient, GET_DESCRIPTOR, with the value word as wValue.
    return queue_control(*kSeat, true, 6 /* GET_DESCRIPTOR */, value, 0, data, length);
}

bool EhciHost::set_configuration(void* seat_handle, uint8_t config_value) {
    auto* const kSeat = static_cast<Seat*>(seat_handle);
    if (kSeat == nullptr || !kSeat->used) {
        return false;
    }
    return queue_control(*kSeat, false, 9 /* SET_CONFIGURATION */,
                         static_cast<uint16_t>(config_value), 0, nullptr, 0);
}

bool EhciHost::configure_interrupt_in(void* seat_handle, uint16_t max_packet,
                                       uint8_t interval) {
    auto* const kSeat = static_cast<Seat*>(seat_handle);
    if (kSeat == nullptr || !kSeat->used || interval == 0 || max_packet == 0) {
        return false;
    }
    if (pfl_phys_ == 0) {
        const base::PhysAddr kPage = mm::Pmm::self().allocate_page();
        if (kPage == base::PhysAddr{}) {
            return false;
        }
        pfl_phys_ = kPage.raw;
    }
    // One interrupt QH for endpoint one: device address, high speed, the
    // descriptor's max packet, data toggle from the qTD. No head-of-list
    // flag here, this QH lives in the periodic schedule, not the reclaim
    // loop. The emulator walks it every frame the list points at it.
    const unsigned long kVirt  = cinux::arch::space::DirectMapVirt(kSeat->page_phys);
    auto* const         kWords = reinterpret_cast<volatile uint32_t*>(kVirt + kQhIntr);
    base::SetBytes(reinterpret_cast<void*>(kVirt + kQhIntr), 0, 0x80);
    kWords[1] = max_packet << kQhMpsShift | kQhDataToggleControl | kQhSpeedHigh |
                (1U << kQhEndpointShift) | kSeat->address;
    kSeat->intr_linked = true;
    kSeat->intr_toggle = false;
    relink_periodic();
    // The status bit lags the enable until the next frame boundary, and
    // that boundary only ticks while the machine's other workers get a
    // turn: park the core each round so the frame timer can run.
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        if ((read32(op_base_ + kRegUsbStatus) & kPeriodicStatus) != 0) {
            return true;
        }
        arch::CpuIdle();
    }
    last_fail_ = 11;
    return false;
}

void EhciHost::arm_interrupt_in(void* seat_handle, uint64_t buffer_physical,
                                unsigned long length,
                                void (*done)(void*, const TransferDone&),
                                void* owner) {
    auto* const kSeat = static_cast<Seat*>(seat_handle);
    if (kSeat == nullptr || !kSeat->used || done == nullptr || length == 0 ||
        length > 0x1000 || (buffer_physical & 0xFFFU) + length > 0x1000) {
        return;
    }
    const unsigned long kVirt    = cinux::arch::space::DirectMapVirt(kSeat->page_phys);
    auto* const         kQh      = reinterpret_cast<volatile uint32_t*>(kVirt + kQhIntr);
    auto* const         kQtd     = reinterpret_cast<volatile uint32_t*>(kVirt + kQtdIntr);
    const unsigned long kQtdPhys = kSeat->page_phys + kQtdIntr;
    kQtd[0] = kLinkTerminated;
    kQtd[1] = kLinkTerminated;
    // The toggle bit rides the token: DATA0 first, DATA1 after.
    kQtd[2] = kTokenActive | kOnComplete | (kTokenPidIn << kPidShift) |
              (static_cast<uint32_t>(length) << kBytesShift) |
              (kSeat->intr_toggle ? kDataToggleOne : 0U);
    kQtd[3] = static_cast<uint32_t>(buffer_physical);
    kQh[3] = static_cast<uint32_t>(kQtdPhys);
    kQh[4] = static_cast<uint32_t>(kQtdPhys);
    kSeat->intr_toggle = !kSeat->intr_toggle;
    kSeat->report_target = static_cast<unsigned long>(buffer_physical);
    kSeat->report_bytes  = length;
    kSeat->done          = done;
    kSeat->owner         = owner;
    arch::MemoryBarrier();
    kSeat->intr_live = true;
}

void EhciHost::relink_periodic() {
    unsigned long head = 0;
    unsigned long prev = 0;
    for (Seat& seat : seats_) {
        if (!seat.used || !seat.intr_linked) {
            continue;
        }
        const unsigned long kQhPhys = seat.page_phys + kQhIntr;
        if (head == 0) {
            head = kQhPhys;
        } else {
            auto* const kPrevWords = reinterpret_cast<volatile uint32_t*>(
                cinux::arch::space::DirectMapVirt(prev));
            kPrevWords[0] = static_cast<uint32_t>(kQhPhys) | kLinkQueueHead;
        }
        prev = kQhPhys;
    }
    if (prev != 0) {
        auto* const kTailWords =
            reinterpret_cast<volatile uint32_t*>(cinux::arch::space::DirectMapVirt(prev));
        kTailWords[0] = kLinkTerminated;
    }
    const uint32_t kEntry =
        head != 0 ? static_cast<uint32_t>(head) | kLinkQueueHead : kLinkTerminated;
    auto* const kList =
        reinterpret_cast<volatile uint32_t*>(cinux::arch::space::DirectMapVirt(pfl_phys_));
    for (unsigned long index = 0; index < kFrameEntries; ++index) {
        kList[index] = kEntry;
    }
    arch::MemoryBarrier();
    write32(op_base_ + kRegPeriodicList, static_cast<uint32_t>(pfl_phys_));
    if (head != 0) {
        // The emulator's frame timer only re-arms itself while some
        // interrupt cause is enabled: claim the frame-list-rollover one
        // so enabling the schedule wakes the timer on the next boundary.
        write32(op_base_ + kRegIntrEnable, 1U << 2U);
        write32(op_base_ + kRegUsbCommand,
                read32(op_base_ + kRegUsbCommand) | kPeriodicEnable);
    } else {
        write32(op_base_ + kRegUsbCommand,
                read32(op_base_ + kRegUsbCommand) & ~kPeriodicEnable);
        static_cast<void>(wait_clear(op_base_ + kRegUsbStatus, kPeriodicStatus));
    }
}

void EhciHost::detach(void* seat_handle) {
    auto* const kSeat = static_cast<Seat*>(seat_handle);
    if (kSeat == nullptr || !kSeat->used) {
        return;
    }
    // Leave the periodic chain before anything else forgets this QH.
    if (kSeat->intr_linked) {
        kSeat->intr_linked = false;
        relink_periodic();
    }
    // Never return a page to the PMM while the controller can still fetch it.
    if (!stop_async()) {
        return;
    }
    if (kSeat->page_phys != 0) {
        mm::Pmm::self().free_page(base::PhysAddr{kSeat->page_phys});
    }
    *kSeat = Seat{};
}

void EhciHost::pump() {
    for (Seat& seat : seats_) {
        if (!seat.used || !seat.intr_live) {
            continue;
        }
        const unsigned long kVirt = cinux::arch::space::DirectMapVirt(seat.page_phys);
        auto* const kQtd = reinterpret_cast<volatile uint32_t*>(kVirt + kQtdIntr);
        if ((kQtd[2] & kTokenActive) != 0) {
            continue;
        }
        seat.intr_live = false;
        const uint32_t kBad = kTokenHalted | kTokenDataBuf | kTokenBabble | kTokenXactErr |
                              kTokenMissed | kTokenSplit;
        const uint32_t kToken = kQtd[2];
        // The token's byte count reads as what REMAINED; what arrived is
        // the difference against what was armed.
        const unsigned long kRemained = (kToken >> kBytesShift) & 0x3FFFU;
        if (seat.done != nullptr) {
            seat.done(seat.owner,
                      TransferDone{.ok = (kToken & kBad) == 0,
                                   .length =
                                       kRemained < seat.report_bytes
                                           ? seat.report_bytes - kRemained
                                           : 0});
        }
    }
}

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

bool EhciHost::port_reset(unsigned int port) {
    const unsigned long kReg = op_base_ + kRegPortSc + ((port - 1) * 4U);
    // The emulator ends a bus reset when the guest drops the reset bit
    // itself, so drive the line for a while and then write it low.
    write32(kReg, kPortPower | kPortReset);
    for (unsigned long spin = 0; spin < 100000; ++spin) {
        arch::CpuRelax();
    }
    write32(kReg, kPortPower);
    const uint32_t kFinal = read32(kReg);
    last_fail_ = (kFinal & kPortEnabled) != 0 ? 0 : 4;
    return (kFinal & kPortEnabled) != 0;
}

bool EhciHost::ring_doorbell() {
    write32(op_base_ + kRegUsbStatus, kAsyncAdvance);
    write32(op_base_ + kRegUsbCommand, read32(op_base_ + kRegUsbCommand) | kAsyncDoorbell);
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        if ((read32(op_base_ + kRegUsbStatus) & kAsyncAdvance) != 0) {
            write32(op_base_ + kRegUsbStatus, kAsyncAdvance);
            return true;
        }
        arch::CpuRelax();
    }
    return false;
}

bool EhciHost::stop_async() {
    write32(op_base_ + kRegUsbCommand, read32(op_base_ + kRegUsbCommand) & ~kAsyncEnable);
    if (!wait_clear(op_base_ + kRegUsbStatus, kAsyncStatus)) {
        last_fail_ = 9;
        unhealthy_ = true;
        return false;
    }
    arch::MemoryBarrier();
    write32(op_base_ + kRegAsyncList, 0);
    return true;
}

bool EhciHost::queue_control(Seat& seat, bool in, uint8_t request, uint16_t value,
                             uint16_t index, void* data, uint16_t length) {
    // Only IN data stages and no-data OUT requests are implemented. With
    // bufptr[1..4] zero, the transfer must fit after offset 0x800 in this page.
    if (length > kControlCapacity || (!in && length != 0)) {
        last_fail_ = 10;
        return false;
    }
    if (!stop_async()) {
        return false;
    }
    last_fail_ = 0;
    const unsigned long kVirt     = cinux::arch::space::DirectMapVirt(seat.page_phys);
    auto* const         kWords    = reinterpret_cast<volatile uint32_t*>(kVirt + kQhOffset);
    auto* const         kSetup    = reinterpret_cast<volatile uint32_t*>(kVirt + kQtdSetup);
    auto* const         kData     = reinterpret_cast<volatile uint32_t*>(kVirt + kQtdData);
    auto* const         kStatus   = reinterpret_cast<volatile uint32_t*>(kVirt + kQtdStatus);
    auto* const         kBuffer   = reinterpret_cast<unsigned char*>(kVirt + kControlBuffer);
    const unsigned long kDataPhys = seat.page_phys + kControlBuffer;

    base::SetBytes(reinterpret_cast<void*>(kVirt), 0, kQhIntr);

    // The setup packet's eight bytes live at the data buffer's head; the
    // data stage, when present, borrows the same page.
    kBuffer[0] = static_cast<unsigned char>(in ? 0x80U : 0x00U);
    kBuffer[1] = request;
    kBuffer[2] = static_cast<unsigned char>(value & 0xFFU);
    kBuffer[3] = static_cast<unsigned char>(value >> 8U);
    kBuffer[4] = static_cast<unsigned char>(index & 0xFFU);
    kBuffer[5] = static_cast<unsigned char>(index >> 8U);
    kBuffer[6] = static_cast<unsigned char>(length & 0xFFU);
    kBuffer[7] = static_cast<unsigned char>(length >> 8U);

    // One QH, head of its own asynchronous list, endpoint zero at the
    // seat's current bus address, high speed: this lane's devices all
    // sit on a high-speed bus behind the emulator's built-in translator.
    kWords[0] = static_cast<uint32_t>(seat.page_phys) | kLinkQueueHead;
    kWords[1] = (kEndpointZeroMps << kQhMpsShift) | kQhDataToggleControl |
                kQhSpeedHigh | kQhHeadOfList |
                (kEndpointZero << kQhEndpointShift) | seat.address;
    kWords[2] = 0;
    kWords[3] = 0;
    kWords[4] = static_cast<uint32_t>(seat.page_phys + kQtdSetup);
    kWords[5] = kLinkTerminated;

    // DTC makes the qTD supply the toggle: SETUP DATA0, then DATA1.
    kSetup[0] = kLinkTerminated;
    kSetup[1] = kLinkTerminated;
    kSetup[2] = kTokenActive | (kTokenPidSetup << kPidShift) | (8U << kBytesShift);
    kSetup[3] = static_cast<uint32_t>(kDataPhys);

    // Data stage: only when bytes travel beyond the setup packet.
    if (length != 0) {
        kData[0] = kLinkTerminated;
        kData[1] = static_cast<uint32_t>(seat.page_phys + kQtdStatus);
        kData[2] = kTokenActive | kDataToggleOne |
                   (kTokenPidIn << kPidShift) |
                   (static_cast<uint32_t>(length) << kBytesShift);
        kData[3] = kDataPhys;
        kSetup[0] = static_cast<uint32_t>(seat.page_phys + kQtdData);
    }

    // Status stage: the opposite direction, interrupt on completion.
    kStatus[0] = kLinkTerminated;
    kStatus[1] = kLinkTerminated;
    kStatus[2] = kTokenActive | kOnComplete | kDataToggleOne |
                 ((in ? kTokenPidOut : kTokenPidIn) << kPidShift);
    if (length != 0) {
        kData[0] = static_cast<uint32_t>(seat.page_phys + kQtdStatus);
    } else {
        kSetup[0] = static_cast<uint32_t>(seat.page_phys + kQtdStatus);
    }

    // Publish the packet bytes and descriptors before enabling DMA.
    arch::MemoryBarrier();
    write32(op_base_ + kRegAsyncList,
            static_cast<uint32_t>(seat.page_phys) | kLinkQueueHead);
    write32(op_base_ + kRegUsbCommand, read32(op_base_ + kRegUsbCommand) | kAsyncEnable);
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        if ((read32(op_base_ + kRegUsbStatus) & kAsyncStatus) != 0) {
            break;
        }
        arch::CpuRelax();
    }
    if ((read32(op_base_ + kRegUsbStatus) & kAsyncStatus) == 0) {
        if (stop_async()) {
            last_fail_ = 5;
        }
        unhealthy_ = true;
        return false;
    }
    // A stalled SETUP/data qTD halts the QH and leaves status active.
    // Notice that halt instead of waiting for an unreachable status stage.
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        if ((kStatus[2] & kTokenActive) == 0 || (kWords[6] & kTokenHalted) != 0) {
            break;
        }
        arch::CpuRelax();
    }
    const bool kHalted = (kWords[6] & kTokenHalted) != 0;
    const bool kComplete = (kStatus[2] & kTokenActive) == 0;
    // Ring only after completion, then stop this single-QH schedule before
    // the next request rewrites its overlay or detach releases its page.
    const bool kAdvanced = kComplete && !kHalted && ring_doorbell();
    if (!stop_async()) {
        return false;
    }
    if (kHalted || !kComplete || !kAdvanced) {
        last_fail_ = kHalted ? 7 : (!kComplete ? 6 : 8);
        unhealthy_ = true;
        return false;
    }

    const uint32_t kBad = kTokenActive | kTokenHalted | kTokenDataBuf | kTokenBabble |
                          kTokenXactErr | kTokenMissed | kTokenSplit;
    if ((kSetup[2] & kBad) != 0 || (kStatus[2] & kBad) != 0 ||
        (length != 0 && (kData[2] & kBad) != 0)) {
        cinux::print::Println("[ehci] transfer refused, tokens %X %X %X", kSetup[2],
                              kData[2], kStatus[2]);
        last_fail_ = 7;
        unhealthy_ = true;
        return false;
    }
    if (in && data != nullptr && length != 0) {
        base::CopyBytes(data, kBuffer, length);
    }
    return true;
}

}  // namespace cinux::driver::usb
