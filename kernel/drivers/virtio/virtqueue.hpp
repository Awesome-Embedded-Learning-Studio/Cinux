/**
 * @file kernel/drivers/virtio/virtqueue.hpp
 * @brief VirtQueue -- VirtIO split virtqueue (F5-M2 batch 1)
 *
 * A split virtqueue is three DMA-coherent tables the device and driver share:
 *   - descriptor table: array of {addr, len, flags, next} scatter-gather entries
 *   - available ring:   driver-written indices into the desc table (requests)
 *   - used ring:        device-written indices + lengths (completions)
 *
 * Ownership model:
 *   - driver owns avail->idx (free-running) and reads used->idx
 *   - device owns used->idx and reads avail->idx
 *   - both wrap modulo queue_size
 *
 * Completion.  Batch 1 polls: wait_completion() spins until used->idx catches
 * up to the submitted avail count.  Batch 3 replaces the spin with an MSI-X
 * interrupt + wait-queue wake.  Unlike NVMe's CQ phase tag, the VirtIO used
 * ring is a free-running idx, so no phase flipping.
 *
 * Storage.  The three tables are independent DmaBuffers (each page-aligned),
 * mirroring NvmeController's admin_sq_buf_/admin_cq_buf_ pattern.  avail/used
 * headers + variable-length rings are accessed by offset (the ring length is
 * runtime-sized, so no flexible-array-member struct is exposed).
 *
 * Namespace: cinux::drivers::virtio
 */

#pragma once

#include <stdint.h>

#include <cinux/expected.hpp>

#include "kernel/drivers/dma/dma_buffer.hpp"
#include "kernel/drivers/virtio/virtio.hpp"

namespace cinux::drivers::virtio {

// ============================================================
// Descriptor flags (VqDesc.flags)
// ============================================================
namespace DescFlag {
constexpr uint16_t NEXT  = 0x01;  ///< this desc chains to desc[next]
constexpr uint16_t WRITE = 0x02;  ///< device-written (device -> driver direction)
}  // namespace DescFlag

// ============================================================
// Ring flags (avail/used flags)
// ============================================================
namespace RingFlag {
constexpr uint16_t NO_INTERRUPT = 0x01;  ///< avail: suppress notify-on-consume
}  // namespace RingFlag

#pragma pack(push, 1)

/// Descriptor table entry (16 bytes).  `addr` is the DMA address of the buffer
/// the device reads (device-read) or writes (DescFlag::WRITE) from/to.
struct VqDesc {
    uint64_t addr;    ///< DMA address of the buffer
    uint32_t len;     ///< length in bytes
    uint16_t flags;
    uint16_t next;    ///< next desc index (valid iff flags & NEXT)
};
static_assert(sizeof(VqDesc) == 16, "VqDesc must be 16 bytes");

/// Used ring completion entry (8 bytes).
struct VqUsedElem {
    uint32_t id;    ///< head desc index of the completed chain
    uint32_t len;   ///< total bytes written (for WRITE descs)
};
static_assert(sizeof(VqUsedElem) == 8, "VqUsedElem must be 8 bytes");

#pragma pack(pop)

// avail/used ring header offsets (dynamic-size rings, accessed by offset).
namespace AvailOff {
constexpr uint32_t FLAGS     = 0;   ///< uint16
constexpr uint32_t IDX       = 2;   ///< uint16 (free-running, driver increments)
constexpr uint32_t RING      = 4;   ///< uint16[size] -- indices into desc table
// used_event uint16 follows at RING + 2*size (only meaningful with EVENT_IDX).
}  // namespace AvailOff

namespace UsedOff {
constexpr uint32_t FLAGS = 0;  ///< uint16
constexpr uint32_t IDX   = 2;  ///< uint16 (free-running, device increments)
constexpr uint32_t RING  = 4;  ///< VqUsedElem[size]
// avail_event uint16 follows at RING + 8*size.
}  // namespace UsedOff

/// DMA byte sizes for the three tables of a @p qsize-entry queue.  Each is
/// allocated independently (page-aligned by DmaPool), so no inter-table padding.
constexpr uint64_t desc_table_bytes(uint16_t qsize) {
    return static_cast<uint64_t>(qsize) * sizeof(VqDesc);
}
constexpr uint64_t avail_ring_bytes(uint16_t qsize) {
    return 4 + 2 * static_cast<uint64_t>(qsize) + 2;  // flags + idx + ring + used_event
}
constexpr uint64_t used_ring_bytes(uint16_t qsize) {
    return 4 + sizeof(VqUsedElem) * static_cast<uint64_t>(qsize) + 2;  // + avail_event
}

/**
 * @brief A VirtIO split virtqueue backed by three DMA buffers
 *
 * Lifecycle: init() allocates + zeroes the tables and calls
 * VirtIODevice::setup_queue() to register them with the device + enable the
 * queue.  submit() publishes a desc chain head to the avail ring; wait_completion()
 * polls the used ring for its consumption.
 *
 * The driver-side free-descriptor list is a simple linked list via VqDesc::next;
 * batch 1 supports single-descriptor requests (no chaining), which is all
 * virtio-blk single-sector I/O needs.
 */
class VirtQueue {
public:
    VirtQueue() = default;
    ~VirtQueue() = default;

    VirtQueue(const VirtQueue&) = delete;
    VirtQueue& operator=(const VirtQueue&) = delete;

    /// Allocate the three tables, zero them, and register + enable the queue
    /// via @p dev.  Stores the notify_off returned by setup_queue for kick().
    /// @p qsize MUST be a power of two and <= device max (caller checks).
    cinux::lib::ErrorOr<void> init(VirtIODevice* dev, uint16_t queue_index, uint16_t qsize);

    /// Publish a single buffer of @p len bytes at DMA @p phys to the device.
    /// @p writable sets DescFlag::WRITE (device writes into the buffer).
    /// @return the avail idx this submission landed on (for wait_completion).
    cinux::lib::ErrorOr<uint16_t> submit_one(uint64_t phys, uint32_t len, bool writable);

    /// Kick the queue (notify the device there is work in the avail ring).
    void kick() const;

    /// Poll until the used ring advances past @p avail_idx (the value returned
    /// by submit_one).  @p out_len (optional) receives the bytes-written count.
    /// Returns Error::TimedOut if the device does not consume within the budget.
    cinux::lib::ErrorOr<void> wait_completion(uint16_t avail_idx, uint32_t* out_len = nullptr);

    // -- test hooks ------------------------------------------------------
    uint16_t size() const { return qsize_; }
    uint16_t free_head() const { return free_head_; }
    uint16_t last_used_idx() const { return last_used_idx_; }
    uint16_t avail_idx() const { return avail_idx_; }

private:
    /// Zero all three tables (DMA buffers are not zero-initialised by DmaPool).
    void zero_tables();

    VirtIODevice*              dev_         = nullptr;
    uint16_t                   queue_index_ = 0;
    uint16_t                   qsize_       = 0;
    uint16_t                   notify_off_  = 0;  ///< from setup_queue, for kick()
    uint16_t                   free_head_   = 0;  ///< next free desc index
    uint16_t                   avail_idx_   = 0;  ///< driver's next avail->idx
    uint16_t                   last_used_idx_ = 0;  ///< driver's last processed used->idx

    cinux::drivers::dma::DmaBuffer desc_buf_;   ///< desc table
    cinux::drivers::dma::DmaBuffer avail_buf_;  ///< avail ring
    cinux::drivers::dma::DmaBuffer used_buf_;   ///< used ring
};

}  // namespace cinux::drivers::virtio
