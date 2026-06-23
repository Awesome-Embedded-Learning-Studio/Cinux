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

using cinux::drivers::usb::XhciCapRegs;
using cinux::drivers::usb::XhciInterrupterRegs;
using cinux::drivers::usb::XhciOpRegs;
using cinux::drivers::usb::scratchpad_buf_count;

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

int main() {
    RUN_ALL_TESTS();
    return _tests_failed > 0 ? 1 : 0;
}

#endif  // CINUX_HOST_TEST
