/**
 * @file kernel/drivers/usb/xhci_controller.cpp
 * @brief xHCI controller bring-up: PCI enable + BAR0 map + halt + reset
 *
 * Batch 1C scope.  No rings/interrupts yet -- those land in 2A/2B/2C.  The
 * reset path is verified in QEMU (run-kernel-test-xhci): find_xhci finds the
 * qemu-xhci, init resets it (CNR clears, controller halts), MaxPorts > 0.
 *
 * Namespace: cinux::drivers::usb
 */

#include "xhci_controller.hpp"

#include <stdint.h>

#include "kernel/arch/x86_64/memory_layout.hpp"
#include "kernel/arch/x86_64/paging_config.hpp"
#include "kernel/drivers/pci/pci.hpp"
#include "kernel/drivers/pci/pci_config.hpp"
#include "kernel/lib/kprintf.hpp"
#include "kernel/mm/vmm.hpp"

namespace cinux::drivers::usb {

XHCIController* XHCIController::s_instance_ = nullptr;

XHCIController& XHCIController::instance() {
    return *s_instance_;
}

void XHCIController::set_instance(XHCIController* c) {
    s_instance_ = c;
}

namespace {
// MMIO sub-allocation inside the KMEM_MMIO window (AHCI @+0x0, LAPIC @+0x10000,
// IOAPIC @+0x11000, MSI-X Table @+0x21000, PBA @+0x22000).  xHCI BAR0 claims
// +0x20000.  4 pages (16 KB) covers capability + operational + runtime +
// doorbell blocks for QEMU qemu-xhci.
constexpr uint64_t kXhciMmioVirt = cinux::arch::KMEM_MMIO_BASE + 0x20000;
constexpr uint64_t kMmioFlags =
    cinux::arch::FLAG_PRESENT | cinux::arch::FLAG_WRITABLE | cinux::arch::FLAG_PCD;
constexpr uint64_t kMmioPages  = 4;
constexpr uint32_t kResetIters = 1000000;  // bounded poll cap (QEMU completes in <<100)
constexpr uint64_t kPageSize   = 4096;
}  // namespace

cinux::lib::ErrorOr<void> XHCIController::init(const pci::PCIDevice& dev) {
    // 1. Enable PCI Bus Master (DMA) + Memory Space (MMIO response).
    const uint32_t cmd = pci::PCI::pci_read(dev.bus, dev.slot, dev.func, pci::PciReg::COMMAND);
    pci::PCI::pci_write(dev.bus, dev.slot, dev.func, pci::PciReg::COMMAND,
                        cmd | pci::PciCmd::BUS_MASTER | pci::PciCmd::MEM_SPACE);

    // 2. Map BAR0 into the MMIO window.
    const uint64_t bar0 = dev.bar[0];
    for (uint64_t i = 0; i < kMmioPages; ++i) {
        if (!cinux::mm::g_vmm.map(kXhciMmioVirt + i * kPageSize, bar0 + i * kPageSize,
                                  kMmioFlags)) {
            return cinux::lib::Error::OutOfMemory;
        }
    }

    cap_regs_              = reinterpret_cast<XhciCapRegs*>(kXhciMmioVirt);
    const uint32_t cap_len = cap_regs_->cap_length_version & 0xFF;
    op_regs_               = reinterpret_cast<XhciOpRegs*>(kXhciMmioVirt + cap_len);
    doorbells_             = reinterpret_cast<volatile uint32_t*>(kXhciMmioVirt + cap_regs_->dboff);
    ir0_ = reinterpret_cast<XhciInterrupterRegs*>(kXhciMmioVirt + cap_regs_->rtsoff + 0x20);

    const uint32_t hcs1 = cap_regs_->hcsparams1;
    max_slots_          = static_cast<uint8_t>(hcs1 & 0xFF);
    max_ports_          = static_cast<uint8_t>((hcs1 >> 24) & 0xFF);

    cinux::lib::kprintf(
        "[xHCI] BAR0=0x%lx CAPLENGTH=%u MaxSlots=%u MaxPorts=%u DBOFF=0x%x RTSOFF=0x%x\n",
        static_cast<unsigned long>(bar0), cap_len, max_slots_, max_ports_, cap_regs_->dboff,
        cap_regs_->rtsoff);

    // 3. Halt (USBCMD=0, wait HCH), then reset (HCRST, wait CNR clear + HCRST
    //    self-clear).  BIOS/SMM legacy handoff is skipped -- QEMU qemu-xhci
    //    boots OS-owned; real HW needs the USB Legacy Support ext-cap handoff.
    op_regs_->usbcmd = 0;
    for (uint32_t i = 0; i < kResetIters; ++i) {
        if (op_regs_->usbsts & Usbsts::kHcHalted) {
            break;
        }
    }

    op_regs_->usbcmd = Usbcmd::kHcReset;
    for (uint32_t i = 0; i < kResetIters; ++i) {
        const uint32_t sts = op_regs_->usbsts;
        const uint32_t run = op_regs_->usbcmd;
        if (!(sts & Usbsts::kControllerNotReady) && !(run & Usbcmd::kHcReset)) {
            break;
        }
    }

    // Post-reset: controller must be halted with CNR clear.
    const uint32_t final_sts = op_regs_->usbsts;
    if (!(final_sts & Usbsts::kHcHalted) || (final_sts & Usbsts::kControllerNotReady)) {
        cinux::lib::kprintf("[xHCI] reset failed: USBSTS=0x%x\n", final_sts);
        return cinux::lib::Error::TimedOut;
    }

    cinux::lib::kprintf("[xHCI] controller reset complete (halted, CNR clear)\n");
    return {};
}

}  // namespace cinux::drivers::usb
