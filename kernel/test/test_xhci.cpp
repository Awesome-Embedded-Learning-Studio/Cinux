/**
 * @file kernel/test/test_xhci.cpp
 * @brief QEMU in-kernel integration tests for the xHCI controller (F5-M5)
 *
 * Runs inside the big kernel test suite.  PCI-enumerates an xHCI controller
 * (class 0x0C / subclass 0x03 / prog_if 0x30); if none is present (the default
 * QEMU config has no qemu-xhci) the test SKIPS (no failure).  Under the
 * run-kernel-test-xhci target (-device qemu-xhci ...) it exercises the real
 * bring-up: PCI find + BAR0 map + halt/reset, asserting MaxPorts > 0 and the
 * post-reset halted/CNR-clear state.
 *
 * Preconditions: VMM initialised (g_vmm.init in main_test.cpp) for BAR0 map.
 */

#include <stdint.h>

#include "big_kernel_test.h"
#include "kernel/drivers/pci/pci.hpp"
#include "kernel/drivers/usb/xhci_controller.hpp"
#include "kernel/lib/kprintf.hpp"

using cinux::drivers::pci::PCIDevice;
using cinux::drivers::pci::PCI;
// Usbsts is a namespace, which a using-declaration cannot name (GCC) -- use a
// using-directive for the usb namespace instead.  Same GOTCHA as test_msix.
using namespace cinux::drivers::usb;

// ============================================================
// Test 1: find xHCI + bring up (skip if no controller present)
// ============================================================

namespace test_xhci {

void test_find_and_reset() {
    PCI pci;
    pci.init();

    PCIDevice dev{};
    if (!pci.find_xhci(dev)) {
        cinux::lib::kprintf("[xHCI] no controller present -- skipping reset test\n");
        return;  // default QEMU config has no qemu-xhci: counts as a pass
    }

    XHCIController            xhci;
    cinux::lib::ErrorOr<void> r = xhci.init(dev);
    TEST_ASSERT_TRUE(r.ok());
    TEST_ASSERT_GT(static_cast<unsigned>(xhci.max_ports()), 0u);

    // Post-reset: controller halted (HCH set), CNR clear.
    const uint32_t sts = xhci.op_regs()->usbsts;
    TEST_ASSERT_TRUE((sts & Usbsts::kHcHalted) != 0);
    TEST_ASSERT_FALSE((sts & Usbsts::kControllerNotReady) != 0);

    // Bring up rings + DCBAA + interrupter and run (Batch 2B).
    cinux::lib::ErrorOr<void> rs = xhci.start();
    TEST_ASSERT_TRUE(rs.ok());
    const uint32_t run_sts = xhci.op_regs()->usbsts;
    TEST_ASSERT_FALSE((run_sts & Usbsts::kHcHalted) != 0);  // now running

    cinux::lib::kprintf("[xHCI] bring-up test passed: MaxPorts=%u run USBSTS=0x%x\n",
                        static_cast<unsigned>(xhci.max_ports()), run_sts);

    // Batch 2C: submit a NOOP command + ring doorbell 0.  The controller
    // executes it, writes a Command Completion Event to the event ring, and
    // raises the event interrupt (USBSTS.EINT).  The test kernel keeps CPU
    // interrupts off, so we poll the event ring directly and observe EINT;
    // live MSI-X -> CPU delivery (handler runs) is proven in the production
    // kernel (Batch 5A, which has sti + APIC).
    xhci.submit_command(0, 0, trb_control(TrbType::kNoOp));

    for (uint32_t i = 0; i < 1000000; ++i) {
        xhci.poll_events();
        if (xhci.cmd_completions() > 0) {
            break;
        }
    }
    const uint32_t usbsts = xhci.op_regs()->usbsts;
    cinux::lib::kprintf("[xHCI] command pipeline: cmd_completions=%u EINT=%d USBSTS=0x%x\n",
                        xhci.cmd_completions(),
                        static_cast<int>((usbsts & Usbsts::kEventInterrupt) != 0), usbsts);

    TEST_ASSERT_GT(xhci.cmd_completions(), 0u);  // Command Completion Event arrived
    // EINT (USBSTS bit 3) set => the controller asserted an event-ring interrupt.
    TEST_ASSERT_TRUE((usbsts & Usbsts::kEventInterrupt) != 0);
}

}  // namespace test_xhci

extern "C" void run_xhci_tests() {
    TEST_SECTION("xHCI");
    RUN_TEST(test_xhci::test_find_and_reset);
    TEST_SUMMARY();
}
