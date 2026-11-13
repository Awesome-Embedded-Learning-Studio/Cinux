/**
 * @file    xhci.cpp
 * @brief   The xHCI controller layer: registers, rings, and the drain.
 *
 * Register offsets relative to each region are the only thing
 * hand-written here; region bases come from the capability block, DMA
 * pages come from the physical allocator, and the ring protocol lives
 * in xhci_layout. Sixty-four-bit registers take two thirty-two-bit
 * writes, low half first so the high write commits the complete
 * address. Transfer events route to seated devices through one erased
 * seam, command completions to the waiter that asked. Delivery arms
 * MSI-X for the day real interrupts arrive, and the drain carries the
 * cargo meanwhile — the v1 boundary kept honestly.
 *
 * @author  Charliechen114514
 * @date    2026-10-22
 * @version 0.2
 * @since   0.6.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#include "kernel/drivers/usb/xhci.hpp"

#include <stdint.h>

#include "cinux/addr.hpp"
#include "cinux/bit_ops/bitmask.hpp"
#include "cinux/memory.hpp"
#include "cinux/ptr.hpp"
#include "cinux/result.hpp"
#include "kernel/arch/instructions.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/platform/interrupt.hpp"
#include "kernel/platform/bus.hpp"
#include "kernel/drivers/usb/usb_config.hpp"
#include "kernel/drivers/usb/xhci_layout.hpp"
#include "kernel/interrupt/irq.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vmm.hpp"

namespace cinux::driver {

namespace {

/// @brief Operational register: run and command bits.
constexpr unsigned long kRegUsbCommand = 0x00;

/// @brief Operational register: halt and readiness bits.
constexpr unsigned long kRegUsbStatus = 0x04;

/// @brief Operational register: command ring control, sixty-four bits.
constexpr unsigned long kRegCrcr = 0x18;

/// @brief Operational register: device context array base, sixty-four bits.
constexpr unsigned long kRegDcbaap = 0x30;

/// @brief Operational register: how many slots this boot enables.
constexpr unsigned long kRegConfig = 0x38;

/// @brief Runtime register, interrupter zero: interrupt management.
constexpr unsigned long kRegIman = 0x00;

constexpr unsigned long kInterrupterZero = 0x20;

constexpr uint32_t kHostSystemError = cinux::base::bit::MaskBit<uint32_t>(2).raw;

constexpr uint64_t kEventHandlerBusy = cinux::base::bit::MaskBit<uint64_t>(3).raw;

/// @brief Runtime register, interrupter zero: segment table size.
constexpr unsigned long kRegErstsz = 0x08;

/// @brief Runtime register, interrupter zero: segment table base.
constexpr unsigned long kRegErstba = 0x10;

/// @brief Runtime register, interrupter zero: dequeue pointer.
constexpr unsigned long kRegErdp = 0x18;

/// @brief USBCMD bit: start the controller.
constexpr uint32_t kRunStop = 1U;

/// @brief USBCMD bit: reset the controller.
constexpr uint32_t kHostReset = cinux::base::bit::MaskBit<uint32_t>(1).raw;

/// @brief USBSTS bit: the controller has halted.
constexpr uint32_t kHcHalted = 1U;

/// @brief USBSTS bit: the controller is still busy with a reset.
constexpr uint32_t kNotReady = cinux::base::bit::MaskBit<uint32_t>(11).raw;

/// @brief IMAN bit: let this interrupter deliver.
constexpr uint32_t kInterruptEnable = cinux::base::bit::MaskBit<uint32_t>(1).raw;

/// @brief Where the ERST entry sits inside the event page, ring above it.
constexpr unsigned long kErstOffset = 0x800;

/// @brief One page of zeros, the honest size of a DMA page.
constexpr unsigned long kPageBytes = 4096;

/// @brief One page of the MSI-X table window, enough for this boot.
constexpr unsigned long kMsixWindowBytes = 4096;

/// @brief Bounded spins for a hypervisor that answers in microseconds.
constexpr unsigned long kSpinLimit = 2000000;

/// @brief The command ring's target: doorbell zero, stream zero.
constexpr unsigned long kCommandDoorbell = 0;

/// @brief Operational register base: the port status registers.
constexpr unsigned long kRegPortSc = 0x400;

/// @brief Port register stride, one bank per root-hub port.
constexpr unsigned long kPortStride = 0x10;

/// @brief PORTSC bit: reset signaling, self-clearing.
constexpr uint32_t kPortReset = cinux::base::bit::MaskBit<uint32_t>(4).raw;

/// @brief PORTSC field: the attached device's speed code.
constexpr cinux::base::bit::BitRange kPortSpeed{.low = 10, .width = 4};

/// @brief The PCI shape this controller answers as.
constexpr uint8_t kPciSubclassUsbLocal = 0x03;

/// @brief Where the control data buffer sits inside the endpoint page.
constexpr unsigned long kControlBufferOffset = 0x800;

/// @brief Where the report buffer sits inside the interrupt page.
constexpr unsigned long kReportSeatOffset = 0x800;

/// @brief The interrupt endpoint's doorbell target: endpoint zero is one.
constexpr unsigned long kInterruptEndpointId = 3;

/// @brief The control endpoint's doorbell target.
constexpr unsigned long kControlEndpointId = 1;

/// @brief Port speed code for a high-speed device.
constexpr uint8_t kSpeedHigh = 3;

/// @brief Endpoint zero's packet ceiling at high speed.
constexpr uint16_t kPacketHigh = 64;

/// @brief Endpoint zero's packet ceiling otherwise.
constexpr uint16_t kPacketElse = 8;

/// @brief The transfer-length field of an event's status word.
constexpr cinux::base::bit::BitRange kEventLength{.low = 0, .width = 24};

/// @brief Lays one control transfer's stages on a ring, status last.
// NOLINTNEXTLINE(readability-function-size) three stages named one by one
void control_stages(usb::Trb* ring, usb::RingState& state, uint64_t ring_physical, uint64_t setup,
                    uint32_t setup_extra, uint64_t buffer, uint32_t buffer_length, bool has_data,
                    uint32_t slot_bits, uint64_t& status_trb) {
    usb::EnqueueCommand(
        ring, usb::kTransferRingTrbs, state, ring_physical, setup, usb::kSetupPacketBytes,
        usb::TrbControl(usb::kTrbSetupStage, setup_extra | usb::kTrbImmediateData |
                                                 usb::kTrbChain | slot_bits));
    if (has_data) {
        usb::EnqueueCommand(ring, usb::kTransferRingTrbs, state, ring_physical, buffer,
                            buffer_length,
                            usb::TrbControl(usb::kTrbDataStage,
                                            usb::kTrbDirIn | usb::kTrbChain | slot_bits));
    }
    status_trb = ring_physical + (state.index * sizeof(usb::Trb));
    usb::EnqueueCommand(ring, usb::kTransferRingTrbs, state, ring_physical, 0, 0,
                        usb::TrbControl(usb::kTrbStatusStage,
                                        usb::kTrbDirIn | usb::kTrbInterruptOnCompletion |
                                            slot_bits));
}

/// @brief The drain's message handler: one word, no context.
void drain_thunk() {
    XhciHost::self().drain_events();
}

}  // namespace

bool XhciHost::init() {
    const PciDevice* const kHost =
        PciBus::self().find_by_class(PciClassCode::kSerialBus, kPciSubclassUsbLocal);
    if (kHost == nullptr ||
        !usb::IsXhciController(kHost->class_code, kHost->subclass, kHost->prog_if)) {
        return false;
    }
    PciBus::self().enable_device(*kHost);
    const uint64_t kWindow = BarBase(kHost->bars, 0);
    if (kWindow == 0 || !mm::MapDeviceWindow(kWindow, usb::kXhciWindowBytes)) {
        return false;
    }
    window_  = reinterpret_cast<volatile unsigned char*>(  // NOLINT(performance-no-int-to-ptr)
        mm::IoremapVirt(static_cast<unsigned long>(kWindow)));
    regions_ = usb::LocateRegions(window_);
    shape_   = usb::ReadShape(window_);
    if (!halt_and_reset() || !arm_rings() || !start_controller()) {
        window_ = nullptr;
        return false;
    }
    print::Println("[kern] xhci %X ports %u slots %u running", shape_.version, shape_.max_ports,
                   shape_.max_slots);
    usb::SeatPrimaryHost(this);
    arm_delivery(kHost);
    return true;
}

bool XhciHost::present() {
    return window_ != nullptr;
}

unsigned int XhciHost::max_ports() {
    return shape_.max_ports;
}

bool XhciHost::unhealthy() {
    return window_ != nullptr &&
           (read32(regions_.operational + kRegUsbStatus) & kHostSystemError) != 0;
}

uint32_t XhciHost::read32(unsigned long offset) const {
    return *reinterpret_cast<const volatile uint32_t*>(window_ + offset);
}

void XhciHost::write32(unsigned long offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(window_ + offset) = value;
}

void XhciHost::write64(unsigned long offset, uint64_t value) {
    write32(offset, static_cast<uint32_t>(value & 0xFFFFFFFFU));
    write32(offset + 4, static_cast<uint32_t>(value >> 32U));
}

bool XhciHost::wait_set(unsigned long offset, uint32_t mask) {
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        if ((read32(offset) & mask) != 0) {
            return true;
        }
        arch::CpuRelax();
    }
    return false;
}

bool XhciHost::wait_clear(unsigned long offset, uint32_t mask) {
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        if ((read32(offset) & mask) == 0) {
            return true;
        }
        arch::CpuRelax();
    }
    return false;
}

bool XhciHost::halt_and_reset() {
    write32(regions_.operational + kRegUsbCommand, 0);
    if (!wait_set(regions_.operational + kRegUsbStatus, kHcHalted)) {
        return false;
    }
    write32(regions_.operational + kRegUsbCommand, kHostReset);
    return wait_clear(regions_.operational + kRegUsbCommand, kHostReset) &&
           wait_clear(regions_.operational + kRegUsbStatus, kNotReady);
}

bool XhciHost::arm_rings() {
    const base::PhysAddr kCommand = mm::Pmm::self().allocate_page();
    const base::PhysAddr kEvent   = mm::Pmm::self().allocate_page();
    const base::PhysAddr kDevices = mm::Pmm::self().allocate_page();
    if (kCommand == base::PhysAddr{} || kEvent == base::PhysAddr{} ||
        kDevices == base::PhysAddr{}) {
        if (kCommand != base::PhysAddr{}) {
            mm::Pmm::self().free_page(kCommand);
        }
        if (kEvent != base::PhysAddr{}) {
            mm::Pmm::self().free_page(kEvent);
        }
        if (kDevices != base::PhysAddr{}) {
            mm::Pmm::self().free_page(kDevices);
        }
        print::Println("[kern] xhci ring pages unavailable, controller stays down");
        return false;
    }
    base::SetBytes(base::PtrAt<void>(mm::DirectMapVirt(kCommand.raw)), 0, kPageBytes);
    base::SetBytes(base::PtrAt<void>(mm::DirectMapVirt(kEvent.raw)), 0, kPageBytes);
    base::SetBytes(base::PtrAt<void>(mm::DirectMapVirt(kDevices.raw)), 0, kPageBytes);
    usb::InitRing(base::PtrAt<usb::Trb>(mm::DirectMapVirt(kCommand.raw)), usb::kCommandRingTrbs,
                  kCommand.raw);
    auto* const kTable   = base::PtrAt<usb::ErstEntry>(mm::DirectMapVirt(kEvent.raw + kErstOffset));
    kTable->address_low  = static_cast<uint32_t>(kEvent.raw);
    kTable->address_high = static_cast<uint32_t>(kEvent.raw >> 32U);
    kTable->size         = static_cast<uint32_t>(usb::kEventRingTrbs);
    command_page_        = kCommand.raw;
    event_page_          = kEvent.raw;
    device_page_         = kDevices.raw;
    command_             = usb::RingState{.index = 0, .cycle = true};
    const unsigned int kSlots =
        shape_.max_slots < usb::kXhciSlotsEnabled ? shape_.max_slots : usb::kXhciSlotsEnabled;
    write32(regions_.operational + kRegConfig, kSlots);
    write64(regions_.operational + kRegDcbaap, device_page_);
    write32(regions_.runtime + kInterrupterZero + kRegErstsz, 1);
    write64(regions_.runtime + kInterrupterZero + kRegErstba, event_page_ + kErstOffset);
    write64(regions_.runtime + kInterrupterZero + kRegErdp, event_page_);
    write64(regions_.operational + kRegCrcr, command_page_ | 1U);
    return true;
}

bool XhciHost::start_controller() {
    write32(regions_.operational + kRegUsbCommand, kRunStop);
    return wait_clear(regions_.operational + kRegUsbStatus, kHcHalted);
}

bool XhciHost::run_command(uint64_t parameter, uint32_t control, uint8_t& slot) {
    const uint64_t kTrb = command_page_ + (command_.index * sizeof(usb::Trb));
    usb::EnqueueCommand(base::PtrAt<usb::Trb>(mm::DirectMapVirt(command_page_)),
                        usb::kCommandRingTrbs, command_, command_page_, parameter, 0, control);
    write32(regions_.doorbells + kCommandDoorbell, 0);
    uint8_t code = 0;
    slot         = 0;
    if (!await_command(kTrb, slot, code)) {
        print::Println("[kern] xhci command %X never answered", control);
        return false;
    }
    if (code != usb::kCompletionSuccess) {
        print::Println("[kern] xhci command %X refused, completion %u", control, code);
        return false;
    }
    return true;
}

void XhciHost::write_doorbell(unsigned int slot, unsigned int target) {
    write32(regions_.doorbells + (static_cast<unsigned long>(slot) * 4UL),
            static_cast<uint32_t>(target));
}

uint32_t XhciHost::port_status(unsigned int port) const {
    return read32(regions_.operational + kRegPortSc +
                  (static_cast<unsigned long>(port - 1) * kPortStride));
}

bool XhciHost::reset_port(unsigned int port, uint8_t& speed) {
    const unsigned long kPortSc =
        regions_.operational + kRegPortSc + (static_cast<unsigned long>(port - 1) * kPortStride);
    write32(kPortSc, kPortReset);
    if (!wait_clear(kPortSc, kPortReset)) {
        return false;
    }
    speed = static_cast<uint8_t>(
        cinux::base::bit::BitMask<uint32_t>{read32(kPortSc)}.extract(kPortSpeed));
    return true;
}



void XhciHost::consume_event() {
    ++event_index_;
    if (event_index_ == usb::kEventRingTrbs) {
        event_index_ = 0;
        event_cycle_ = !event_cycle_;
    }
    write64(regions_.runtime + kInterrupterZero + kRegErdp,
            (event_page_ + (event_index_ * sizeof(usb::Trb))) | kEventHandlerBusy);
}

// NOLINTNEXTLINE(readability-function-size) drain names three cargos
void XhciHost::drain_events() {
    const arch::IrqGuard kQuiet;
    const auto           kGuard = event_gate_.guard();
    if (window_ == nullptr || event_page_ == 0) {
        return;
    }
    if ((read32(regions_.operational + kRegUsbStatus) & kHostSystemError) != 0) {
        return;
    }
    const auto* const kEvents = base::PtrAt<const usb::Trb>(mm::DirectMapVirt(event_page_));
    while (true) {
        const usb::Trb& event = kEvents[event_index_];
        if (!usb::EventPending(event, event_cycle_)) {
            return;
        }
        const uint8_t kSlot = usb::SlotIdOf(event);
        const uint8_t kType = usb::TrbType(event);
        consume_event();
        if (kType == usb::kTrbTransferEvent && kSlot >= 1 && kSlot <= usb::kXhciSlotsEnabled &&
            seats_[kSlot - 1].slot != 0) {
            seat_transfer(seats_[kSlot - 1], event);
            continue;
        }
        if (kType == usb::kTrbCommandCompletion && event.parameter == awaited_trb_) {
            command_ready_ = true;
            command_slot_  = kSlot;
            command_code_  = usb::CompletionCode(event);
        }
    }
}

bool XhciHost::await_command(uint64_t trb_address, uint8_t& slot, uint8_t& code) {
    awaited_trb_   = trb_address;
    command_ready_ = false;
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        drain_events();
        if (command_ready_) {
            slot = command_slot_;
            code = command_code_;
            return true;
        }
        if (unhealthy()) {
            return false;
        }
        arch::CpuRelax();
    }
    return false;
}

// NOLINTNEXTLINE(readability-function-size) the wiring names five hands
void XhciHost::arm_delivery(const PciDevice* host) {
    const PciSpot  kSpot{.bus = host->bus, .slot = host->slot, .func = host->func};
    msi::MsixFacts facts{};
    if (!msi::DecodeMsix(&ReadSpotConfig, nullptr, kSpot, facts)) {
        print::Println("[kern] xhci msix absent, the drain carries the cargo");
        return;
    }
    const base::Result<interrupt::MsiVector> kSeated =
        interrupt::Irq::self().seat_message(&drain_thunk);
    if (!kSeated.ok()) {
        print::Println("[kern] xhci message pool full, the drain carries the cargo");
        return;
    }
    const msi::MsixWindows kWindows = msi::ResolveMsixWindows(facts, host->bars);
    if (!mm::MapDeviceWindow(kWindows.table, kMsixWindowBytes)) {
        print::Println("[kern] xhci msix window refused, the drain carries the cargo");
        return;
    }
    auto* const kEntry = base::PtrAt<msi::MsixEntry>(mm::IoremapVirt(kWindows.table));
    if (!msi::ArmMsixEntry(*kEntry, msi::XapicMessageAddress(LocalApic::self().id()),
                           kSeated.value().value)) {
        print::Println("[kern] xhci msix entry stayed masked, the drain carries the cargo");
        return;
    }
    write32(regions_.runtime + kInterrupterZero + kRegIman, kInterruptEnable);
    print::Println("[kern] xhci msix armed, vector %u", kSeated.value().value);
}


// ---------------------------------------------------------------------------
// The neutral HostDriver face: everything a seated device asks of the
// silicon. Slots, contexts and doorbells end their climb right here.
// ---------------------------------------------------------------------------

bool XhciHost::port_connected(unsigned int port) {
    return port >= 1 && port <= max_ports() && (port_status(port) & 1U) != 0;
}

bool XhciHost::port_taken(unsigned int port) {
    for (const usb::XhciSeat& seat : seats_) {
        if (seat.slot != 0 && seat.port == static_cast<uint8_t>(port)) {
            return true;
        }
    }
    return false;
}

void* XhciHost::address(unsigned int port) {
    uint8_t slot = 0;
    if (!run_command(0, usb::TrbControl(usb::kTrbEnableSlot, 0), slot) || slot == 0) {
        return nullptr;
    }
    usb::XhciSeat& seat = seats_[slot - 1];

    const base::PhysAddr kInput  = mm::Pmm::self().allocate_page();
    const base::PhysAddr kOutput = mm::Pmm::self().allocate_page();
    const base::PhysAddr kEp0    = mm::Pmm::self().allocate_page();
    const base::PhysAddr kEp1    = mm::Pmm::self().allocate_page();
    if (kInput == base::PhysAddr{} || kOutput == base::PhysAddr{} ||
        kEp0 == base::PhysAddr{} || kEp1 == base::PhysAddr{}) {
        if (kInput != base::PhysAddr{}) {
            mm::Pmm::self().free_page(kInput);
        }
        if (kOutput != base::PhysAddr{}) {
            mm::Pmm::self().free_page(kOutput);
        }
        if (kEp0 != base::PhysAddr{}) {
            mm::Pmm::self().free_page(kEp0);
        }
        if (kEp1 != base::PhysAddr{}) {
            mm::Pmm::self().free_page(kEp1);
        }
        static_cast<void>(run_command(0, usb::TrbControl(usb::kTrbDisableSlot,
                                                         static_cast<uint32_t>(slot) << 24U),
                                      slot));
        return nullptr;
    }

    seat = usb::XhciSeat{.slot = slot, .port = static_cast<uint8_t>(port)};
    base::SetBytes(base::PtrAt<void>(mm::DirectMapVirt(kInput.raw)), 0, kPageBytes);
    base::SetBytes(base::PtrAt<void>(mm::DirectMapVirt(kOutput.raw)), 0, kPageBytes);
    base::SetBytes(base::PtrAt<void>(mm::DirectMapVirt(kEp0.raw)), 0, kPageBytes);
    base::SetBytes(base::PtrAt<void>(mm::DirectMapVirt(kEp1.raw)), 0, kPageBytes);
    seat.input_page                                                        = kInput.raw;
    seat.output_page                                                       = kOutput.raw;
    seat.ep0_page                                                          = kEp0.raw;
    seat.ep1_page                                                          = kEp1.raw;
    base::PtrAt<uint64_t>(mm::DirectMapVirt(device_page_))[seat.slot] = seat.output_page;

    if (!reset_port(port, seat.speed)) {
        detach(&seat);
        return nullptr;
    }

    auto* const kContext = base::PtrAt<uint32_t>(mm::DirectMapVirt(seat.input_page));
    usb::InitRing(base::PtrAt<usb::Trb>(mm::DirectMapVirt(seat.ep0_page)),
                  usb::kTransferRingTrbs, seat.ep0_page);
    seat.ep0_ = usb::RingState{.index = 0, .cycle = true};
    usb::SetAddFlags(kContext, 0x3U);
    usb::BuildSlotContext(kContext + 8, 0, seat.port, seat.speed, 1);
    usb::BuildControlEpContext(kContext + 16, seat.ep0_page,
                               seat.speed == kSpeedHigh ? kPacketHigh : kPacketElse);
    const uint32_t kSlotBits = static_cast<uint32_t>(seat.slot) << 24U;
    uint8_t        answered  = 0;
    if (!run_command(seat.input_page, usb::TrbControl(usb::kTrbAddressDevice, kSlotBits),
                     answered)) {
        detach(&seat);
        return nullptr;
    }
    return &seat;
}

bool XhciHost::spin_for_control(usb::XhciSeat& seat) {
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        drain_events();
        if (seat.control_done) {
            return seat.control_code == usb::kCompletionSuccess;
        }
        if (unhealthy()) {
            return false;
        }
        arch::CpuRelax();
    }
    return false;
}

bool XhciHost::control_read(void* handle, uint16_t value, void* data, uint16_t length) {
    auto& seat = *static_cast<usb::XhciSeat*>(handle);
    if (seat.slot == 0) {
        return false;
    }
    auto* const    kRing   = base::PtrAt<usb::Trb>(mm::DirectMapVirt(seat.ep0_page));
    const uint64_t kBuffer = seat.ep0_page + kControlBufferOffset;
    const uint32_t kSlot   = static_cast<uint32_t>(seat.slot) << 24U;
    seat.control_trb       = 0;
    seat.control_done      = false;
    seat.control_code      = 0;
    control_stages(kRing, seat.ep0_, seat.ep0_page,
                   usb::PackSetup(0x80, 0x06, value, 0, length), usb::kSetupDataIn << 17U,
                   kBuffer, length, true, kSlot, seat.control_trb);
    write_doorbell(seat.slot, kControlEndpointId);
    if (!spin_for_control(seat)) {
        return false;
    }
    base::CopyBytes(data, base::PtrAt<const void>(mm::DirectMapVirt(kBuffer)), length);
    return true;
}

bool XhciHost::set_configuration(void* handle, uint8_t config_value) {
    auto& seat = *static_cast<usb::XhciSeat*>(handle);
    if (seat.slot == 0) {
        return false;
    }
    auto* const    kRing = base::PtrAt<usb::Trb>(mm::DirectMapVirt(seat.ep0_page));
    const uint32_t kSlot = static_cast<uint32_t>(seat.slot) << 24U;
    seat.control_trb     = 0;
    seat.control_done    = false;
    seat.control_code    = 0;
    control_stages(kRing, seat.ep0_, seat.ep0_page,
                   usb::PackSetup(0x00, 0x09, config_value, 0, 0), usb::kSetupNoData << 17U, 0, 0,
                   false, kSlot, seat.control_trb);
    write_doorbell(seat.slot, kControlEndpointId);
    return spin_for_control(seat);
}

bool XhciHost::configure_interrupt_in(void* handle, uint16_t max_packet, uint8_t interval) {
    auto& seat = *static_cast<usb::XhciSeat*>(handle);
    if (seat.slot == 0 || interval == 0 || (seat.speed >= kSpeedHigh && interval > 16)) {
        return false;
    }
    auto interval_exponent = static_cast<uint8_t>(interval - 1U);
    if (seat.speed < kSpeedHigh) {
        interval_exponent = 3;
        for (unsigned period = interval; period > 1; period /= 2U) {
            ++interval_exponent;
        }
    }
    auto* const kContext = base::PtrAt<uint32_t>(mm::DirectMapVirt(seat.input_page));
    base::SetBytes(kContext, 0, kPageBytes);
    usb::InitRing(base::PtrAt<usb::Trb>(mm::DirectMapVirt(seat.ep1_page)),
                  usb::kTransferRingTrbs, seat.ep1_page);
    seat.ep1_ = usb::RingState{.index = 0, .cycle = true};
    usb::SetAddFlags(kContext, 0x9U);
    usb::BuildSlotContext(kContext + 8, 0, seat.port, seat.speed, kInterruptEndpointId);
    usb::BuildInterruptInEpContext(kContext + 32, seat.ep1_page, max_packet, interval_exponent);
    const uint32_t kSlotBits = static_cast<uint32_t>(seat.slot) << 24U;
    uint8_t        answered  = 0;
    return run_command(seat.input_page,
                       usb::TrbControl(usb::kTrbConfigureEndpoint, kSlotBits), answered);
}

void XhciHost::ring_report(usb::XhciSeat& seat) {
    auto* const kRing = base::PtrAt<usb::Trb>(mm::DirectMapVirt(seat.ep1_page));
    seat.report_trb   = seat.ep1_page + (seat.ep1_.index * sizeof(usb::Trb));
    usb::EnqueueCommand(kRing, usb::kTransferRingTrbs, seat.ep1_, seat.ep1_page,
                        seat.ep1_page + kReportSeatOffset, seat.report_bytes,
                        usb::TrbControl(usb::kTrbNormal, usb::kTrbInterruptOnCompletion));
    write_doorbell(seat.slot, kInterruptEndpointId);
}

void XhciHost::arm_interrupt_in(void* handle, uint64_t buffer_physical, unsigned long length,
                                void (*done)(void* owner, const usb::TransferDone&),
                                void* owner) {
    auto& seat = *static_cast<usb::XhciSeat*>(handle);
    if (seat.slot == 0) {
        return;
    }
    seat.report_target = buffer_physical;
    seat.done          = done;
    seat.owner        = owner;
    seat.report_bytes = length;
    seat.report_live  = true;
    ring_report(seat);
}

void XhciHost::detach(void* handle) {
    auto& seat = *static_cast<usb::XhciSeat*>(handle);
    if (seat.slot == 0) {
        return;
    }
    uint8_t answered = 0;
    if (!run_command(0, usb::TrbControl(usb::kTrbDisableSlot,
                                        static_cast<uint32_t>(seat.slot) << 24U),
                     answered)) {
        return;
    }
    base::PtrAt<uint64_t>(mm::DirectMapVirt(device_page_))[seat.slot] = 0;
    const uint64_t kPages[] = {seat.input_page, seat.output_page, seat.ep0_page, seat.ep1_page};
    for (const uint64_t kPage : kPages) {
        if (kPage != 0) {
            mm::Pmm::self().free_page(base::PhysAddr{kPage});
        }
    }
    seat = usb::XhciSeat{};
}

void XhciHost::pump() {
    drain_events();
}

void XhciHost::seat_transfer(usb::XhciSeat& seat, const usb::Trb& event) {
    if (event.parameter == seat.control_trb && seat.control_trb != 0) {
        seat.control_done = true;
        seat.control_code = usb::CompletionCode(event);
        seat.control_trb  = 0;
        return;
    }
    if (seat.report_live && event.parameter == seat.report_trb) {
        seat.report_live = false;
        seat.report_trb  = 0;
        const uint8_t  kCode = usb::CompletionCode(event);
        const uint32_t kResidual =
            cinux::base::bit::BitMask<uint32_t>{event.status}.extract(kEventLength);
        const unsigned long kLength =
            kResidual <= seat.report_bytes ? seat.report_bytes - kResidual : 0;
        if (kLength != 0 && seat.report_target != 0) {
            // The ring lands the bytes in the endpoint's DMA page; the
            // neutral layer reads them from the page it handed over.
            base::CopyBytes(base::PtrAt<void>(mm::DirectMapVirt(seat.report_target)),
                            base::PtrAt<const void>(mm::DirectMapVirt(seat.ep1_page +
                                                                     kReportSeatOffset)),
                            kLength);
        }
        if (seat.done != nullptr) {
            seat.done(seat.owner,
                      usb::TransferDone{
                          .ok = kCode == usb::kCompletionSuccess ||
                                kCode == usb::kCompletionShortPacket,
                          .length = kLength});
        }
    }
}

}  // namespace cinux::driver
