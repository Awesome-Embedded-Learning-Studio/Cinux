/**
 * @file kernel/drivers/nvme/nvme_io.cpp
 * @brief NVMe NVM command submission and completion polling
 */

#include <stdint.h>

#include "kernel/drivers/hpet/hpet.hpp"
#include "kernel/lib/kprintf.hpp"
#include "nvme.hpp"

namespace cinux::drivers::nvme {

namespace {
constexpr uint16_t kIoQSize         = 64;
constexpr uint32_t kIoFallbackIters = 1'000'000;
constexpr uint64_t kIoTimeoutNs     = 500'000'000;
}  // namespace

bool NvmeController::poll_one_completion(uint16_t expected_cid, uint16_t& out_status,
                                         bool& mismatch_reported) {
    // Poll the IO CQ head once.  Returns true if @p expected_cid's completion is
    // at the head (phase tag flipped to io_cq_phase_): advances the head, strips
    // the phase bit into out_status, and consumes any mismatched entry first.
    // Shared by io_submit (poll mode) and the MSI-X completion IRQ handler
    // (async mode, B2).
    auto*             cq = reinterpret_cast<volatile NvmeCqe*>(io_cq_buf_.virt());
    volatile NvmeCqe& cqe          = cq[io_cq_head_];
    const uint16_t    status_field = cqe.status;
    if ((status_field & 0x1) != io_cq_phase_) {
        return false;  // head entry not yet flipped to our phase: no completion
    }

    const uint16_t completion_cid = cqe.cid;
    if (completion_cid != expected_cid) {
        // A completion for a different command is at our CQ head -- another CPU's
        // io_submit advanced here while we'd yielded, or a stale entry.  Consume
        // it and keep polling for ours; the old "report + continue without
        // advancing" deadlocks once yields let the head move.
        if (!mismatch_reported) {
            cinux::lib::kprintf(
                "[NVMe] CQ CID mismatch raw=0x%x got=%u expected=%u sq_head=%u "
                "sq_id=%u driver_head=%u phase=%u\n",
                status_field, completion_cid, expected_cid, cqe.sq_head, cqe.sq_id,
                io_cq_head_, io_cq_phase_);
            mismatch_reported = true;
        }
        io_cq_head_ = (io_cq_head_ + 1) % kIoQSize;
        if (io_cq_head_ == 0) {
            io_cq_phase_ ^= 1;
        }
        *io_cq_hdbell_ = io_cq_head_;
        return false;
    }

    const uint16_t status = static_cast<uint16_t>(status_field >> 1);
    io_cq_head_           = (io_cq_head_ + 1) % kIoQSize;
    if (io_cq_head_ == 0) {
        io_cq_phase_ ^= 1;
    }
    *io_cq_hdbell_ = io_cq_head_;
    out_status     = status;
    return true;
}

cinux::lib::ErrorOr<uint16_t> NvmeController::io_submit(const NvmeCmd& cmd) {
    // Called under NvmeBlockDevice::lock_ (now a Mutex, batch B1) + holds
    // io_lock_ (Spinlock) for the SQ enqueue + CQ poll.  B1 keeps synchronous
    // poll; B2 swaps the poll loop for blocking on the MSI-X completion IRQ
    // (dma_buf_ stays single-in-flight under the Mutex, so the wait is safe).
    io_lock_.acquire();
    NvmeCmd submitted = cmd;
    submitted.cid     = io_next_cid_++;

    auto* sq        = static_cast<NvmeCmd*>(io_sq_buf_.virt());
    sq[io_sq_tail_] = submitted;
    io_sq_tail_     = (io_sq_tail_ + 1) % kIoQSize;
    *io_sq_tdbell_  = io_sq_tail_;

    const bool     has_deadline = cinux::drivers::g_hpet.available();
    const uint64_t deadline =
        has_deadline ? cinux::drivers::g_hpet.monotonic_ns() + kIoTimeoutNs : 0;
    uint32_t fallback_iters        = 0;
    bool     cid_mismatch_reported = false;
    uint16_t status                = 0;

    while (has_deadline ? cinux::drivers::g_hpet.monotonic_ns() < deadline
                        : fallback_iters++ < kIoFallbackIters) {
        if (poll_one_completion(submitted.cid, status, cid_mismatch_reported)) {
            if (status != 0) {
                const uint64_t slba = static_cast<uint64_t>(submitted.cdw10) |
                                      (static_cast<uint64_t>(submitted.cdw11) << 32);
                const uint16_t nlb  = static_cast<uint16_t>(submitted.cdw12) + 1;
                cinux::lib::kprintf(
                    "[NVMe] CQ error status=0x%x cid=%u cmd(op=0x%x nsid=%u slba=%llu "
                    "nlb=%u)\n",
                    status, submitted.cid, submitted.opcode, submitted.nsid,
                    static_cast<unsigned long long>(slba), nlb);
            }
            io_lock_.release();
            return status;
        }
        __asm__ volatile("pause");
    }

    // Timed out: completion never arrived within kIoTimeoutNs / kIoFallbackIters.
    auto*             cq = reinterpret_cast<volatile NvmeCqe*>(io_cq_buf_.virt());
    volatile NvmeCqe& timed_out_cqe = cq[io_cq_head_];
    const uint64_t    slba =
        static_cast<uint64_t>(submitted.cdw10) | (static_cast<uint64_t>(submitted.cdw11) << 32);
    const uint16_t nlb = static_cast<uint16_t>(submitted.cdw12) + 1;
    cinux::lib::kprintf(
        "[NVMe] CQ timeout raw=0x%x got_cid=%u expected_cid=%u sq_head=%u sq_id=%u "
        "sq_tail=%u driver_head=%u phase=%u cmd(op=0x%x nsid=%u slba=%llu nlb=%u)\n",
        timed_out_cqe.status, timed_out_cqe.cid, submitted.cid, timed_out_cqe.sq_head,
        timed_out_cqe.sq_id, io_sq_tail_, io_cq_head_, io_cq_phase_, submitted.opcode,
        submitted.nsid, static_cast<unsigned long long>(slba), nlb);
    io_lock_.release();
    return cinux::lib::Error::TimedOut;
}

cinux::lib::ErrorOr<void> NvmeController::nvm_io(uint8_t opcode, uint32_t nsid, uint64_t slba,
                                                 uint16_t                        nlb,
                                                 cinux::drivers::dma::DmaBuffer& buf) {
    NvmeCmd cmd{};
    cmd.opcode  = opcode;
    cmd.nsid    = nsid;
    cmd.prp1    = buf.phys();
    cmd.cdw10   = static_cast<uint32_t>(slba);
    cmd.cdw11   = static_cast<uint32_t>(slba >> 32);
    cmd.cdw12   = static_cast<uint32_t>(nlb - 1);
    auto result = io_submit(cmd);
    if (!result.ok()) {
        return result.error();
    }
    if (result.value() != 0) {
        cinux::lib::kprintf(
            "[NVMe] NVM I/O opcode=0x%x nsid=%u slba=%llu nlb=%u failed status=0x%x\n", opcode,
            nsid, static_cast<unsigned long long>(slba), nlb, result.value());
        return cinux::lib::Error::IOError;
    }
    return {};
}

cinux::lib::ErrorOr<void> NvmeController::read_blocks(uint32_t nsid, uint64_t slba, uint16_t nlb,
                                                      cinux::drivers::dma::DmaBuffer& buf) {
    return nvm_io(0x02, nsid, slba, nlb, buf);
}

cinux::lib::ErrorOr<void> NvmeController::write_blocks(uint32_t nsid, uint64_t slba, uint16_t nlb,
                                                       cinux::drivers::dma::DmaBuffer& buf) {
    return nvm_io(0x01, nsid, slba, nlb, buf);
}

}  // namespace cinux::drivers::nvme