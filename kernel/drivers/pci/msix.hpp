/**
 * @file kernel/drivers/pci/msix.hpp
 * @brief PCI MSI-X capability discovery (pure, reader-injected)
 *
 * Walks a PCI function's capability list to locate the MSI-X capability
 * (id 0x11) and decodes its Table/PBA location and size.  Config-space
 * access is injected through a ConfigReader callback so the same code runs
 * in the kernel (real 0xCF8/0xCFC reads) and in host unit tests (mock
 * config-space image).
 *
 * Scope (Batch 0A): discovery only.  Table/PBA MMIO mapping + entry
 * programming (Batch 0B) and the IDT vector-install helper (Batch 0C) are
 * layered on top of this in msix.cpp.
 *
 * Namespace: cinux::drivers::pci::msix
 */

#pragma once

#include <stdint.h>

namespace cinux::drivers::pci::msix {

// ============================================================
// Config-space reader callback
// ============================================================

/// Signature matches PCI::pci_read (pci.hpp): returns the 32-bit dword at
/// the dword-aligned config offset of function bus/slot/func.  Injecting
/// this keeps find_capability() free of any real I/O.
using ConfigReader = uint32_t (*)(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

// ============================================================
// MSI-X Message Control bits (word at cap_offset + 2)
// ============================================================

namespace MsixMsgCtrl {
constexpr uint16_t kTableSizeMask = 0x07FF;    ///< Bits [10:0]: Table Size (entries = field + 1)
constexpr uint16_t kFunctionMask  = 1U << 14;  ///< Bit 14: Function Mask (global masking)
constexpr uint16_t kEnable        = 1U << 15;  ///< Bit 15: MSI-X Enable
}  // namespace MsixMsgCtrl

// ============================================================
// MSI-X Table / PBA BAR-offset decode (dword at cap_offset + 4 / + 8)
// ============================================================

namespace MsixBarOffset {
constexpr uint32_t kBirMask    = 0x00000007;  ///< Bits [2:0]: BAR Indicator Register (0-5)
constexpr uint32_t kOffsetMask = 0xFFFFFFF8;  ///< Bits [31:3]: offset within the BAR (8-aligned)
}  // namespace MsixBarOffset

// ============================================================
// Parsed MSI-X capability
// ============================================================

/**
 * @brief Decoded MSI-X capability for one PCI function
 *
 * Populated by find_capability().  When @p found is false every other field
 * is zero-initialised.
 */
struct MsixCap {
    bool     found;            ///< MSI-X capability present on this function
    uint8_t  cap_offset;       ///< Config-space offset of the MSI-X capability
    uint16_t message_control;  ///< Raw Message Control word
    uint16_t table_size;       ///< Number of Table entries = (MC[10:0] + 1)
    uint32_t table_offset;     ///< Byte offset of the Table within its BAR
    uint8_t  table_bar;        ///< BIR (0-5) of the BAR holding the Table
    uint32_t pba_offset;       ///< Byte offset of the PBA within its BAR
    uint8_t  pba_bar;          ///< BIR of the BAR holding the PBA
};

/**
 * @brief Walk the capability list of a PCI function and locate MSI-X
 *
 * Reads STATUS (0x06) to confirm a capability list exists, then follows the
 * singly-linked list from the pointer at 0x34 until the MSI-X capability
 * (id 0x11) is found or the list ends.  Performs no MMIO or DMA -- only
 * config reads through @p reader -- so it links and runs identically in the
 * kernel and in host unit tests.
 *
 * @param bus     PCI bus number
 * @param slot    PCI slot number
 * @param func    PCI function number
 * @param reader  Config-space dword reader (kernel: &PCI::pci_read; test: mock)
 * @return        Decoded MsixCap (.found=false if MSI-X is absent)
 */
MsixCap find_capability(uint8_t bus, uint8_t slot, uint8_t func, ConfigReader reader);

}  // namespace cinux::drivers::pci::msix
