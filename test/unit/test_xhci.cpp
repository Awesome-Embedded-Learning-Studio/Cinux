/**
 * @file test/unit/test_xhci.cpp
 * @brief Host unit tests for xHCI register layout + pure helpers
 *
 * Verifies the packed MMIO struct sizes (static_asserts in the header fire on
 * include) and the HCSPARAMS2 scratchpad decode.  TRB ring math lands here in
 * Batch 2A.
 *
 * Compile condition: -DCINUX_HOST_TEST
 */

#define TEST_FRAMEWORK_IMPL
#include "test_framework.h"

#ifdef CINUX_HOST_TEST

#    include <cstdint>

#    include "drivers/usb/xhci_registers.hpp"
#    include "drivers/usb/xhci_ring.hpp"

// TrbType is a namespace, which a using-declaration cannot name (GCC) -- use a
// using-directive for the usb namespace instead (same GOTCHA as test_msix).
using namespace cinux::drivers::usb;

// ============================================================
// 1. Packed MMIO struct sizes (match the xHCI spec offsets)
// ============================================================

TEST("xhci: XhciCapRegs is 0x20 bytes") {
    ASSERT_EQ(sizeof(XhciCapRegs), 0x20u);
}

TEST("xhci: XhciOpRegs is 0x3C bytes") {
    ASSERT_EQ(sizeof(XhciOpRegs), 0x3Cu);
}

TEST("xhci: XhciInterrupterRegs is 0x20 bytes") {
    ASSERT_EQ(sizeof(XhciInterrupterRegs), 0x20u);
}

// ============================================================
// 2. HCSPARAMS2 scratchpad decode
// ============================================================

TEST("xhci: scratchpad count 0 when both lo/hi zero") {
    ASSERT_EQ(scratchpad_buf_count(0x00000000), 0u);
}

TEST("xhci: scratchpad count uses lo field only when hi zero") {
    // lo = 3, hi = 0 -> 3
    ASSERT_EQ(scratchpad_buf_count(0x00000003), 3u);
}

TEST("xhci: scratchpad count combines lo | (hi << 5)") {
    // lo = 1, hi = 2 -> 1 | (2 << 5) = 1 | 64 = 65
    ASSERT_EQ(scratchpad_buf_count(0x00020001), 65u);
}

// ============================================================
// 3. TrbRing producer: cycle bit + Link-TRB wrap + PCS flip (Batch 2A)
// ============================================================

TEST("xhci: TrbRing enqueue sets cycle = PCS and records the type") {
    Trb     storage[5];  // 4 usable + 1 Link
    TrbRing ring;
    ring.init(storage, 4, 0x1000);

    ASSERT_TRUE(ring.producer_cycle());  // PCS starts true
    ring.enqueue(0xAA, 0, trb_control(TrbType::kNormal));
    ASSERT_EQ(static_cast<uint32_t>(storage[0].control & kCycleBit), kCycleBit);
    ASSERT_EQ(trb_type(storage[0].control), TrbType::kNormal);
    ASSERT_EQ(static_cast<uint64_t>(storage[0].parameter), 0xAAULL);
}

TEST("xhci: TrbRing wraps via Link TRB and flips PCS") {
    Trb     storage[5];  // 4 usable + Link at [4]
    TrbRing ring;
    ring.init(storage, 4, 0x1000);

    for (uint32_t i = 0; i < 4; ++i) {
        ring.enqueue(i, 0, trb_control(TrbType::kNoOp));
    }
    // After 4 enqueues the producer hit slots_=4 -> Link written, wrap, PCS flip.
    ASSERT_EQ(ring.enqueue_index(), 0u);
    ASSERT_FALSE(ring.producer_cycle());
    ASSERT_EQ(trb_type(storage[4].control), TrbType::kLink);
    ASSERT_TRUE(storage[4].control & kToggleCycle);
    ASSERT_EQ(static_cast<uint64_t>(storage[4].parameter), 0x1000ULL);  // base
}

TEST("xhci: TrbRing second lap uses the flipped cycle bit") {
    Trb     storage[5];
    TrbRing ring;
    ring.init(storage, 4, 0x1000);
    for (uint32_t i = 0; i < 4; ++i) {
        ring.enqueue(i, 0, trb_control(TrbType::kNoOp));  // lap 1
    }
    ring.enqueue(0xBB, 0, trb_control(TrbType::kNormal));  // lap 2, first TRB
    ASSERT_FALSE(storage[0].control & kCycleBit);          // cycle = PCS = 0 (flipped)
    ASSERT_EQ(static_cast<uint64_t>(storage[0].parameter), 0xBBULL);
}

// ============================================================
// 4. EventRing consumer: cycle match + CCS flip on wrap (Batch 2A)
// ============================================================

TEST("xhci: EventRing dequeues events while cycle matches CCS") {
    Trb       storage[4];
    EventRing ring;
    ring.init(storage, 4);
    // Controller wrote two events with cycle = PCS = 1; [2] still empty (cycle 0).
    storage[0].control = trb_control(TrbType::kCommandCompletion) | kCycleBit;
    storage[1].control = trb_control(TrbType::kCommandCompletion) | kCycleBit;
    storage[2].control = 0;

    Trb ev;
    ASSERT_TRUE(ring.dequeue(ev));
    ASSERT_EQ(trb_type(ev.control), TrbType::kCommandCompletion);
    ASSERT_TRUE(ring.dequeue(ev));
    ASSERT_FALSE(ring.dequeue(ev));  // empty
}

TEST("xhci: EventRing flips CCS when the dequeue pointer wraps") {
    Trb       storage[2];  // size 2
    EventRing ring;
    ring.init(storage, 2);
    // Controller fills both (cycle 1), wraps, flips to cycle 0, rewrites [0].
    storage[0].control = trb_control(TrbType::kTransferEvent) | kCycleBit;
    storage[1].control = trb_control(TrbType::kTransferEvent) | kCycleBit;

    Trb ev;
    ASSERT_TRUE(ring.dequeue(ev));
    ASSERT_EQ(ring.dequeue_index(), 1u);
    ASSERT_TRUE(ring.dequeue(ev));  // wraps 1->2->0, CCS flips to false
    ASSERT_EQ(ring.dequeue_index(), 0u);
    ASSERT_FALSE(ring.consumer_cycle());

    // New lap: controller writes [0] with cycle 0 (matches new CCS).
    storage[0].control = trb_control(TrbType::kTransferEvent);  // cycle 0
    ASSERT_TRUE(ring.dequeue(ev));
}

int main() {
    RUN_ALL_TESTS();
    return _tests_failed > 0 ? 1 : 0;
}

#endif  // CINUX_HOST_TEST
