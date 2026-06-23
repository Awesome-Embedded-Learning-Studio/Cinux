/**
 * @file kernel/drivers/usb/xhci_trb.hpp
 * @brief xHCI Transfer Request Block (TRB) layout + type/bit constants
 *
 * Every command, transfer and event is a 16-byte TRB.  The Cycle bit (control
 * [0]) is the ring handshaking flag; TRB Type lives in control [15:10].  Ring
 * data structures (xhci_ring.hpp) layer the cycle-bit / Link-TRB mechanics on
 * top of an array of Trb.  Pure types -- host-compilable.
 *
 * Namespace: cinux::drivers::usb
 */

#pragma once

#include <stdint.h>

namespace cinux::drivers::usb {

/// One xHCI TRB (16 bytes).  Members are volatile: both the CPU and controller
/// access TRBs through a DMA region.
struct Trb {
    volatile uint64_t parameter;  ///< +0:  data pointer / command args / event params
    volatile uint32_t status;     ///< +8:  transfer length / completion code
    volatile uint32_t control;    ///< +12: [0]=Cycle, [15:10]=TRB Type, + flags
};
static_assert(sizeof(Trb) == 16, "TRB must be 16 bytes");

// ============================================================
// TRB Type values (control [15:10])
// ============================================================

namespace TrbType {
// Transfer-ring TRBs
constexpr uint32_t kNormal            = 1;  ///< Normal (bulk/interrupt data)
constexpr uint32_t kSetup             = 2;  ///< Setup Stage (control transfer)
constexpr uint32_t kData              = 3;  ///< Data Stage
constexpr uint32_t kStatus            = 4;  ///< Status Stage
constexpr uint32_t kLink              = 6;  ///< Link (ring closure)
constexpr uint32_t kNoOp              = 8;  ///< No-Op (transfer ring / simple command)
// Command-ring TRBs
constexpr uint32_t kEnableSlot        = 9;
constexpr uint32_t kAddressDevice     = 11;
constexpr uint32_t kConfigureEndpoint = 12;
// Event-ring TRBs (events produced by the controller)
constexpr uint32_t kTransferEvent     = 32;
constexpr uint32_t kCommandCompletion = 33;
constexpr uint32_t kPortStatusChange  = 34;
}  // namespace TrbType

// ============================================================
// Control-word bit constants
// ============================================================

constexpr uint32_t kCycleBit              = 1U << 0;  ///< C: ownership handshake bit
constexpr uint32_t kToggleCycle           = 1U << 1;  ///< Link TRB: consumer flips its cycle
constexpr uint32_t kInterruptOnCompletion = 1U << 2;  ///< IOC: raise interrupt on completion
constexpr uint32_t kImmediateData = 1U << 4;  ///< IDT (Normal/Data): data inline in parameter
constexpr uint32_t kChain         = 1U << 5;  ///< CH: next TRB is part of this transfer
constexpr uint32_t kTrbTypeShift  = 10;

/// Build a control word from a TRB type + flags (no cycle bit -- the ring sets
/// that from the producer cycle state).
inline uint32_t trb_control(uint32_t type, uint32_t flags = 0) {
    return (type << kTrbTypeShift) | flags;
}

/// Extract the TRB type field from a control word.
inline uint32_t trb_type(uint32_t control) {
    return (control >> kTrbTypeShift) & 0x3F;
}

}  // namespace cinux::drivers::usb
