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
    auto*             cq           = reinterpret_cast<volatile NvmeCqe*>(io_cq_buf_.virt());
    volatile NvmeCqe& cqe          = cq[io_cq_head_];
    const uint16_t    status_field = cqe.status;
    if ((status_field & 0x1) != io_cq_phase_) {
        return false;  // head entry not yet flipped to our phase: no completion
    }
    // The controller publishes the phase bit with the rest of the CQE. Do not
    // let later CID/status loads move before observing that ownership change.
    __atomic_thread_fence(__ATOMIC_ACQUIRE);

    const uint16_t completion_cid = cqe.cid;
    if (completion_cid != expected_cid) {
        if (!mismatch_reported) {
            cinux::lib::kprintf(
                "[NVMe] CQ CID mismatch raw=0x%x got=%u expected=%u sq_head=%u "
                "sq_id=%u driver_head=%u phase=%u\n",
                status_field, completion_cid, expected_cid, cqe.sq_head, cqe.sq_id, io_cq_head_,
                io_cq_phase_);
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
    if (io_irq_mode_) {
        // Production (B2): IRQ-driven and deliberately single in-flight. The
        // controller owns this serialization instead of relying on every caller
        // to wrap it correctly. The short spinlock protects SQ publication; the
        // sleepable mutex remains held until this exact CID completes.
        auto submit_guard = io_submit_mutex_.guard();
        io_lock_.acquire();
        const uint16_t submitted_cid = io_next_cid_++;
        __atomic_store_n(&io_completion_ready_, false, __ATOMIC_RELEASE);
        __atomic_store_n(&io_pending_cid_, submitted_cid, __ATOMIC_RELEASE);
        __atomic_store_n(&io_request_active_, true, __ATOMIC_RELEASE);
        NvmeCmd submitted = cmd;
        submitted.cid     = submitted_cid;
        auto* sq          = static_cast<NvmeCmd*>(io_sq_buf_.virt());
        sq[io_sq_tail_]   = submitted;
        io_sq_tail_       = (io_sq_tail_ + 1) % kIoQSize;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        *io_sq_tdbell_ = io_sq_tail_;
        io_lock_.release();

        // A generic Scheduler::unblock must never be mistaken for an NVMe
        // completion. Semaphore::wait validates its own post grant, and this
        // predicate additionally binds that grant to the submitted CID.
        for (;;) {
            io_completion_sema_.wait();
            if (__atomic_load_n(&io_completion_ready_, __ATOMIC_ACQUIRE) &&
                __atomic_load_n(&io_completed_cid_, __ATOMIC_RELAXED) == submitted_cid) {
                return __atomic_load_n(&io_last_status_, __ATOMIC_RELAXED);
            }
        }
    }

    // Test kernel (IF=0, MSI-X masked): synchronous poll.  Held under
    // NvmeBlockDevice::lock_ (Mutex) + io_lock_ (Spinlock) for the poll.
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
                    "[NVMe] CQ error status=0x%x cid=%u cmd(op=0x%x nsid=%u slba=%llu nlb=%u)\n",
                    status, submitted.cid, submitted.opcode, submitted.nsid,
                    static_cast<unsigned long long>(slba), nlb);
            }
            io_lock_.release();
            return status;
        }
        __asm__ volatile("pause");
    }

    auto*             cq            = reinterpret_cast<volatile NvmeCqe*>(io_cq_buf_.virt());
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
