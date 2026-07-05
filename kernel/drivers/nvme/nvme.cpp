/**
 * @file kernel/drivers/nvme/nvme.cpp
 * @brief NvmeController bring-up: BAR0 assign + PCI enable + map + CAP/VS read (F5-M3 batch 1)
 *
 * Modelled on XHCIController::init (PCI COMMAND enable -> g_vmm.map BAR0 ->
 * reinterpret_cast).  BAR0 is mapped at KMEM_MMIO+0x70000 (4 pages = 16 KB),
 * avoiding every existing MMIO sub-allocation (AHCI/xHCI/MSI-X/e1000/HPET).
 *
 * BAR self-assignment.  The CinuxOS PCI layer reads BARs but does not assign
 * them -- it relies on SeaBIOS, which configures AHCI/e1000 but NOT the QEMU
 * nvme controller (dev.bar[0] reads 0).  Reading CAP/VS through an unassigned
 * BAR maps phys 0x0 (low RAM) and returns garbage that still satisfies a naive
 * "> 0" assertion (a false-green the batch-1 mechanism test exists to catch).
 * NVMe therefore self-assigns BAR0 via the standard probe (write all-1s, read
 * back the size bits) before mapping.  A generic PCI BAR allocator is a future
 * PCI-subsystem task; this self-assign is scoped to NVMe.
 */

#include "kernel/drivers/nvme/nvme.hpp"

#include <stdint.h>

#include "kernel/arch/x86_64/memory_layout.hpp"
#include "kernel/arch/x86_64/paging_config.hpp"
#include "kernel/drivers/pci/pci_config.hpp"
#include "kernel/lib/kprintf.hpp"
#include "kernel/mm/vmm.hpp"

namespace cinux::drivers::nvme {

namespace {
// KMEM_MMIO sub-allocation for the NVMe BAR0 register window.  MUST NOT collide
// with existing slots (AHCI @+0x0, LAPIC @+0x10000, IOAPIC @+0x11000, xHCI BAR0
// @+0x20000, MSI-X @+0x40000, e1000 @+0x50000, HPET @+0x60000).  4 pages = 16 KB
// covers the controller register window + the admin doorbells (the per-queue
// doorbell stride is derived from CAP.DSTRD in batch 2).
constexpr uint64_t kNvmeMmioVirt = cinux::arch::KMEM_MMIO_BASE + 0x70000;
constexpr uint64_t kNvmeBarPages = 4;
constexpr uint64_t kPageSize     = 4096;
constexpr uint64_t kMmioFlags =
    cinux::arch::FLAG_PRESENT | cinux::arch::FLAG_WRITABLE | cinux::arch::FLAG_PCD;

// Fixed 16 KB-aligned slot in the QEMU 32-bit PCI MMIO window for BAR0
// self-assignment, clear of AHCI BAR5 @0xfebf1000.  NVMe BAR0 is a 64-bit BAR
// but the QEMU register window is < 4 GB, so a 32-bit assign suffices (the high
// 32 bits stay zero).
constexpr uint32_t kAssignedBar0 = 0xfeb40000;
}  // namespace

cinux::lib::ErrorOr<void> NvmeController::init(const pci::PCIDevice& dev) {
    // 1. Ensure BAR0 has an MMIO address.  If the BIOS left it unassigned
    //    (dev.bar[0] == 0 -- SeaBIOS skips the QEMU nvme controller), probe the
    //    size and assign a fixed slot ourselves.
    uint64_t bar0 = dev.bar[0];
    if ((bar0 & pci::BAR_ADDR_MASK_32) == 0) {
        pci::PCI::pci_write(dev.bus, dev.slot, dev.func, pci::PciReg::BAR0, 0xFFFFFFFFu);
        pci::PCI::pci_write(dev.bus, dev.slot, dev.func, pci::PciReg::BAR1, 0xFFFFFFFFu);
        const uint32_t probe = pci::PCI::pci_read(dev.bus, dev.slot, dev.func, pci::PciReg::BAR0);
        const uint32_t size  = ~(probe & pci::BAR_ADDR_MASK_32) + 1;
        pci::PCI::pci_write(dev.bus, dev.slot, dev.func, pci::PciReg::BAR0, kAssignedBar0);
        pci::PCI::pci_write(dev.bus, dev.slot, dev.func, pci::PciReg::BAR1, 0);
        bar0               = kAssignedBar0;
        const uint32_t rb0 = pci::PCI::pci_read(dev.bus, dev.slot, dev.func, pci::PciReg::BAR0);
        const uint32_t rb1 = pci::PCI::pci_read(dev.bus, dev.slot, dev.func, pci::PciReg::BAR1);
        cinux::lib::kprintf(
            "[NVMe] BAR0 was unassigned (probe size=%u) -> 0x%x (rb0=0x%x rb1=0x%x)\n",
            static_cast<unsigned>(size), kAssignedBar0, rb0, rb1);
    }

    // 2. Enable PCI Bus Master + Memory Space so the controller may master DMA
    //    and expose its MMIO window.
    const uint32_t cmd = pci::PCI::pci_read(dev.bus, dev.slot, dev.func, pci::PciReg::COMMAND);
    pci::PCI::pci_write(dev.bus, dev.slot, dev.func, pci::PciReg::COMMAND,
                        cmd | pci::PciCmd::BUS_MASTER | pci::PciCmd::MEM_SPACE);

    // 3. Map BAR0 into the MMIO window (uncached).
    for (uint64_t i = 0; i < kNvmeBarPages; ++i) {
        if (!cinux::mm::g_vmm.map(kNvmeMmioVirt + i * kPageSize, bar0 + i * kPageSize,
                                  kMmioFlags)) {
            cinux::lib::kprintf("[NVMe] BAR0 map failed at page %u\n", static_cast<unsigned>(i));
            return cinux::lib::Error::IOError;
        }
    }
    regs_ = reinterpret_cast<volatile NvmeRegs*>(kNvmeMmioVirt);

    // 4. Read CAP/VS to confirm the window is live (mechanism test, batch 1).
    const uint32_t cap_lo = regs_->cap_lo;
    const uint32_t vs     = regs_->vs;
    cinux::lib::kprintf("[NVMe] BAR0=0x%lx CAP.MQES=%u DSTRD=%u VS=%u.%u\n",
                        static_cast<unsigned long>(bar0), static_cast<unsigned>(cap_lo & 0xFFFF),
                        static_cast<unsigned>((cap_lo >> 24) & 0xF),
                        static_cast<unsigned>((vs >> 16) & 0xFFFF),
                        static_cast<unsigned>((vs >> 8) & 0xFF));  // MNR = VS bits[15:8]
    return {};
}

}  // namespace cinux::drivers::nvme
