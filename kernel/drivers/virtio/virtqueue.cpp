/**
 * @file kernel/drivers/virtio/virtqueue.cpp
 * @brief VirtQueue -- split virtqueue implementation (F5-M2 batch 1)
 *
 * Three DMA-coherent tables (desc / avail / used) shared between driver and
 * device.  submit_one() publishes a single-buffer request (no scatter-gather
 * chaining in batch 1 -- sufficient for virtio-blk single-sector I/O);
 * wait_completion() polls the used ring's free-running idx until the device
 * consumes the submission.  Real interrupts replace the poll in batch 3.
 */

#include "kernel/drivers/virtio/virtqueue.hpp"

#include <stdint.h>

#include "kernel/drivers/dma/dma_pool.hpp"
#include "kernel/lib/kprintf.hpp"

namespace cinux::drivers::virtio {

namespace {
constexpr uint32_t kPollIters = 1000000;  ///< device-completion spin budget (no IRQ yet)

void zero_dma(void* p, uint64_t bytes) {
    auto b = static_cast<volatile uint64_t*>(p);
    for (uint64_t i = 0; i < bytes / 8; ++i) {
        b[i] = 0;
    }
}
}  // namespace

void VirtQueue::zero_tables() {
    zero_dma(desc_buf_.virt(), desc_buf_.size());
    zero_dma(avail_buf_.virt(), avail_buf_.size());
    zero_dma(used_buf_.virt(), used_buf_.size());
}

cinux::lib::ErrorOr<void> VirtQueue::init(VirtIODevice* dev, uint16_t queue_index, uint16_t qsize) {
    dev_         = dev;
    queue_index_ = queue_index;
    qsize_       = qsize;

    auto dr = cinux::drivers::dma::g_dma_pool.alloc(desc_table_bytes(qsize));
    auto ar = cinux::drivers::dma::g_dma_pool.alloc(avail_ring_bytes(qsize));
    auto ur = cinux::drivers::dma::g_dma_pool.alloc(used_ring_bytes(qsize));
    if (!dr.ok() || !ar.ok() || !ur.ok()) {
        cinux::lib::kprintf("[VirtQueue] DMA alloc failed (qsz=%u)\n", qsize);
        return cinux::lib::Error::OutOfMemory;
    }
    desc_buf_  = std::move(dr.value());
    avail_buf_ = std::move(ar.value());
    used_buf_  = std::move(ur.value());
    zero_tables();

    // Build the free-descriptor list (chain via VqDesc::next) so submit_one
    // can pop the head.  Single-descriptor requests clear NEXT, so the chain
    // only matters for recycling (batch 1 advances without recycling -- 64
    // descs is plenty for the bring-up mechanism test).
    auto* desc = static_cast<volatile VqDesc*>(desc_buf_.virt());
    for (uint16_t i = 0; i < qsize; ++i) {
        desc[i].next = static_cast<uint16_t>((i + 1) % qsize);
    }
    free_head_     = 0;
    avail_idx_     = 0;
    last_used_idx_ = 0;

    // Register the ring addresses with the device + read the notify offset,
    // then enable the queue.  The device will start consuming avail entries
    // as soon as DRIVER_OK is set + we kick.
    auto sr = dev_->setup_queue(queue_index, qsize, desc_buf_.phys(), avail_buf_.phys(),
                                used_buf_.phys());
    if (!sr.ok()) {
        return sr.error();
    }
    notify_off_ = sr.value();
    dev_->enable_queue(queue_index);
    return {};
}

cinux::lib::ErrorOr<uint16_t> VirtQueue::submit_one(uint64_t phys, uint32_t len, bool writable) {
    // 1. Pop a free descriptor + fill it (single desc, no NEXT chain).
    auto* desc        = static_cast<volatile VqDesc*>(desc_buf_.virt());
    const uint16_t di = free_head_;
    desc[di].addr  = phys;
    desc[di].len   = len;
    desc[di].flags = writable ? DescFlag::WRITE : 0;
    free_head_     = desc[di].next;  // advance (no recycle in batch 1)

    // 2. Publish the desc index to the avail ring + bump the driver's idx.
    auto*        avail = static_cast<volatile uint8_t*>(avail_buf_.virt());
    const uint16_t slot = static_cast<uint16_t>(avail_idx_ % qsize_);
    auto*        ring  = reinterpret_cast<volatile uint16_t*>(avail + AvailOff::RING);
    ring[slot]         = di;
    const uint16_t published = static_cast<uint16_t>(avail_idx_ + 1);  // wraps at 65536
    *reinterpret_cast<volatile uint16_t*>(avail + AvailOff::IDX) = published;
    avail_idx_ = published;
    return published;
}

void VirtQueue::kick() const {
    dev_->notify_queue(queue_index_, notify_off_);
}

cinux::lib::ErrorOr<void> VirtQueue::wait_completion(uint16_t target, uint32_t* out_len) {
    auto* used = static_cast<volatile uint8_t*>(used_buf_.virt());
    for (uint32_t i = 0; i < kPollIters; ++i) {
        const uint16_t used_idx =
            *reinterpret_cast<volatile uint16_t*>(used + UsedOff::IDX);
        if (used_idx == target) {
            if (out_len != nullptr) {
                // The just-consumed elem lives at (target - 1) mod qsize.
                const uint16_t          slot = static_cast<uint16_t>(target - 1);
                auto* ring = reinterpret_cast<volatile VqUsedElem*>(used + UsedOff::RING);
                *out_len = ring[slot % qsize_].len;
            }
            last_used_idx_ = target;
            return {};
        }
    }
    cinux::lib::kprintf("[VirtQueue] wait_completion timeout (target=%u used=%u)\n", target,
                        *reinterpret_cast<volatile uint16_t*>(used + UsedOff::IDX));
    return cinux::lib::Error::TimedOut;
}

}  // namespace cinux::drivers::virtio
