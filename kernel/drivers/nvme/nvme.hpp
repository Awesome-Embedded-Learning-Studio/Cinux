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
 *   2b -- Identify Controller via the admin queue (doorbell + CQ poll).
 *   3-5 -- MSI-X, IO queues, IBlockDevice adapter.
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
/// live at BAR0 + 0x1000 + queue_id * (2 * stride) and are computed in enable()
/// from CAP.DSTRD; they are NOT part of this fixed struct.
struct NvmeRegs {
    uint32_t cap_lo;  ///< 0x00 CAP low  (MQES[15:0], CQR[16], AMS[18:17], TO[27:23], DSTRD[31:28])
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

/// Admin Submission Queue Entry (64 bytes).  NVMe 1.x command format.
struct NvmeCmd {
    uint8_t  opcode;     ///< 0x00 e.g. kAdminIdentify = 0x06
    uint8_t  flags;      ///< 0x01 FUSE + PSDT (0)
    uint16_t cid;        ///< 0x02 Command Identifier
    uint32_t nsid;       ///< 0x04 Namespace ID
    uint64_t reserved0;  ///< 0x08 CDW02-03 (reserved)
    uint64_t mptr;       ///< 0x10 Metadata Pointer
    uint64_t prp1;       ///< 0x18 PRP Entry 1
    uint64_t prp2;       ///< 0x20 PRP Entry 2
    uint32_t cdw10;      ///< 0x28
    uint32_t cdw11;      ///< 0x2C
    uint32_t cdw12;      ///< 0x30
    uint32_t cdw13;      ///< 0x34
    uint32_t cdw14;      ///< 0x38
    uint32_t cdw15;      ///< 0x3C
};
static_assert(sizeof(NvmeCmd) == 64, "NVMe SQE must be 64 bytes");

/// Admin Completion Queue Entry (16 bytes).
struct NvmeCqe {
    uint32_t cdw0;      ///< 0x00 command-specific
    uint32_t reserved;  ///< 0x04
    uint16_t sq_head;   ///< 0x08 SQ Head pointer when the command completed
    uint16_t sq_id;     ///< 0x0A SQ Identifier
    uint16_t cq_id;     ///< 0x0C CQ Identifier
    uint16_t status;    ///< 0x0E bit0 = phase tag, bits[15:1] = SC/SCT
};
static_assert(sizeof(NvmeCqe) == 16, "NVMe CQE must be 16 bytes");

/// Partial Identify Controller data (offset 0x00-0x3F).  The full structure is
/// 4 KiB; we read only the identifying fields the batch-2b mechanism test needs.
struct IdentifyController {
    uint16_t vid;     ///< 0x00 PCI Vendor ID
    uint16_t ssvid;   ///< 0x02 PCI Subsystem Vendor ID
    uint8_t  sn[20];  ///< 0x04 Serial Number (ASCII)
    uint8_t  mn[40];  ///< 0x18 Model Number (ASCII)
};
static_assert(sizeof(IdentifyController) == 64, "IdentifyController header (vid/ssvid/sn/mn)");

class NvmeController {
public:
    /// Map BAR0 into the MMIO window, enable Bus Master + Memory Space, and read
    /// CAP/VS to confirm the register window is live.  Does NOT enable the
    /// controller -- call enable() (batch 2a).
    cinux::lib::ErrorOr<void> init(const pci::PCIDevice& dev);

    /// Enable the controller (CC.EN -> wait CSTS.RDY=1) and configure the Admin
    /// Submission/Completion queues + doorbell pointers.  Batch 2a.
    cinux::lib::ErrorOr<void> enable();

    /// Submit an Identify Controller command on the admin queue and poll the CQ
    /// for completion (batch 2b).  Fills @p out with the identify data.
    cinux::lib::ErrorOr<void> identify_controller(IdentifyController& out);

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
    // Admin doorbell pointers + ring state.  Batch 2b.
    volatile uint32_t*             admin_sq_tdbell_ = nullptr;
    volatile uint32_t*             admin_cq_hdbell_ = nullptr;
    uint32_t                       admin_sq_tail_   = 0;
    uint32_t                       admin_cq_head_   = 0;
    uint8_t                        cq_phase_        = 1;  // NVMe CQ starts at phase = 1
    uint32_t                       doorbell_stride_ = 0;
};

}  // namespace cinux::drivers::nvme
