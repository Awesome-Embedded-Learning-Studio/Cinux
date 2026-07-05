/**
 * @file kernel/drivers/nvme/nvme.hpp
 * @brief NvmeController -- PCIe NVMe controller bring-up (F5-M3)
 *
 * Brings up a single NVMe controller discovered via PCI: enables Bus Master +
 * Memory Space, maps BAR0 into the MMIO window, and reads CAP/VS to confirm the
 * register window is live.  Modelled on XHCIController (instance + init(const
 * PCIDevice&)) -- NOT all-static, because NVMe carries per-controller mutable
 * state (admin/IO queues, doorbells).
 *
 * Batches:
 *   1  -- PCI find + BAR0 self-assign + map + CAP/VS read.
 *   2a -- controller enable (CC.EN <-> CSTS.RDY) + Admin SQ/CQ config.
 *   2b -- Identify Controller via the admin queue (doorbell + poll).
 *   3-5 -- MSI-X, IO queues, IBlockDevice adapter.
 *
 * The IBlockDevice path is zero-change to ext2: Ext2 already holds a raw
 * IBlockDevice* (Ext2::Ext2(IBlockDevice* dev)), so a future NvmeBlockDevice is
 * handed to Ext2 directly.
 *
 * Namespace: cinux::drivers::nvme
 */

#pragma once

#include <stdint.h>

#include <cinux/expected.hpp>

#include "kernel/drivers/dma/dma_buffer.hpp"
#include "kernel/drivers/pci/pci.hpp"

namespace cinux::drivers::nvme {

/// Controller registers (NVMe 1.x, MMIO offset from BAR0).  Doorbell registers
/// live at BAR0 + 0x1000 + queue_id * (2 * stride) and are computed on demand
/// (batch 2b); they are NOT part of this fixed struct.
struct NvmeRegs {
    uint32_t cap_lo;  ///< 0x00 CAP low  (MQES[15:0], CQR[16], AMS[18:17], TO[23:20], DSTRD[31:24])
    uint32_t cap_hi;  ///< 0x04 CAP high (CSS, BPS, MPS8, ...)
    uint32_t vs;      ///< 0x08 Version (MJR[31:16].MNR[15:8].TER[7:0]); e.g. 0x00010400 = NVMe 1.4
    uint32_t intms;   ///< 0x0C Interrupt Mask Set
    uint32_t intmc;   ///< 0x10 Interrupt Mask Clear
    uint32_t cc;      ///< 0x14 Controller Configuration
    uint32_t reserved0;  ///< 0x18
    uint32_t csts;       ///< 0x1C Controller Status (RDY[0], CFS[1], SHST[4:2])
    uint32_t nssr;       ///< 0x20 NVM Subsystem Reset
    uint32_t aqa;        ///< 0x24 Admin Queue Attributes (ASQS[27:16].ACQS[11:0], 0-based)
    uint32_t asq_lo;     ///< 0x28 Admin SQ Base Address (low 32)
    uint32_t asq_hi;     ///< 0x2C
    uint32_t acq_lo;     ///< 0x30 Admin CQ Base Address (low 32)
    uint32_t acq_hi;     ///< 0x34
};

class NvmeController {
public:
    /// Map BAR0 into the MMIO window, enable Bus Master + Memory Space, and read
    /// CAP/VS to confirm the register window is live.  Does NOT enable the
    /// controller -- call enable() (batch 2a).
    cinux::lib::ErrorOr<void> init(const pci::PCIDevice& dev);

    /// Enable the controller (CC.EN -> wait CSTS.RDY=1) and configure the Admin
    /// Submission/Completion queues.  Leaves the controller ready to accept
    /// admin commands (batch 2a).  Identify/IO arrive in 2b/4.
    cinux::lib::ErrorOr<void> enable();

    bool present() const { return regs_ != nullptr; }

    /// CAP.MQES (0-based; the maximum queue size is MQES + 1).
    uint16_t mqes() const { return static_cast<uint16_t>(regs_->cap_lo & 0xFFFF); }

    /// Version register (MJR[31:16].MNR[15:8]); e.g. 0x00010400 = NVMe 1.4.
    uint32_t version() const { return regs_->vs; }

    volatile NvmeRegs* regs() const { return regs_; }

private:
    volatile NvmeRegs*             regs_ = nullptr;
    // Admin SQ/CQ (4 KiB DMA each).  Batch 2a.
    cinux::drivers::dma::DmaBuffer admin_sq_buf_;
    cinux::drivers::dma::DmaBuffer admin_cq_buf_;
};

}  // namespace cinux::drivers::nvme
