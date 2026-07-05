/**
 * @file kernel/test/test_nvme.cpp
 * @brief QEMU in-kernel integration tests for the NVMe controller (F5-M3)
 *
 * Runs inside the big kernel test suite.  PCI-enumerates an NVMe controller
 * (class 0x01 / subclass 0x08); if none is present the test SKIPS (no failure).
 * Under the run-kernel-test-all target (-device nvme ...) it exercises the
 * batch-1 bring-up: PCI find + BAR0 map + CAP/VS read, asserting MQES > 0 and
 * the Version register encodes NVMe 1.x+ (the register window is truly mapped,
 * not a zero/reserved read).
 *
 * Preconditions: VMM initialised (g_vmm.init in main_test.cpp) for BAR0 map.
 */

#include <stdint.h>

#include "big_kernel_test.h"
#include "kernel/drivers/nvme/nvme.hpp"
#include "kernel/drivers/pci/pci.hpp"
#include "kernel/lib/kprintf.hpp"

using cinux::drivers::pci::PCIDevice;
using cinux::drivers::pci::PCI;
using namespace cinux::drivers;
using namespace cinux::drivers::nvme;

namespace test_nvme {

// ============================================================
// Test 1: find NVMe + map BAR0 + read CAP/VS (skip if no controller present)
// ============================================================

void test_find_and_map() {
    PCI pci;
    pci.init();

    PCIDevice dev{};
    if (!pci.find_nvme(dev)) {
        cinux::lib::kprintf("[NVMe] no controller present -- skipping init test\n");
        return;  // default config without -device nvme: counts as a pass (skip)
    }

    // PCI class check -- the matcher is class+subclass, not prog_if.
    TEST_ASSERT_EQ(dev.class_code, pci::PciClass::MASS_STORAGE);
    TEST_ASSERT_EQ(dev.subclass, pci::PciClass::NVME_SUBCLASS);

    NvmeController            ctrl;
    cinux::lib::ErrorOr<void> r = ctrl.init(dev);
    TEST_ASSERT_TRUE(r.ok());
    TEST_ASSERT_TRUE(ctrl.present());

    // batch 2a: controller enable (CC.EN <-> CSTS.RDY) + Admin SQ/CQ config.
    cinux::lib::ErrorOr<void> e = ctrl.enable();
    TEST_ASSERT_TRUE(e.ok());
    TEST_ASSERT_TRUE((ctrl.regs()->csts & 0x1) != 0);  // CSTS.RDY set

    // batch 2b: Identify Controller via the admin queue (doorbell + CQ poll).
    nvme::IdentifyController  id{};
    cinux::lib::ErrorOr<void> ir = ctrl.identify_controller(id);
    TEST_ASSERT_TRUE(ir.ok());
    TEST_ASSERT_GT(static_cast<unsigned>(id.vid), 0u);  // real vendor ID
    cinux::lib::kprintf("[NVMe] Identify mechanism: VID=0x%x SSVID=0x%x\n",
                        static_cast<unsigned>(id.vid), static_cast<unsigned>(id.ssvid));

    // batch 3: MSI-X multi-instance (MsixController @+0x74000, entry 0 -> 0x41).
    cinux::lib::ErrorOr<void> mr = ctrl.init_msi_x();
    TEST_ASSERT_TRUE(mr.ok());
    TEST_ASSERT_TRUE(ctrl.msix_table() != nullptr);
    TEST_ASSERT_TRUE(ctrl.msix_table()[0].msg_addr_lower != 0);

    // CAP.MQES (0-based) > 0: real controllers allow >= 2 entries; a zero or
    // unmapped read returns 0, so MQES > 0 proves the window is live and the
    // controller answered.  VS encodes MJR.MNR; QEMU nvme reports 1.x or later.
    const uint16_t mqes = ctrl.mqes();
    const uint32_t vs   = ctrl.version();
    cinux::lib::kprintf("[NVMe] mechanism: MQES=%u VS=0x%x\n", static_cast<unsigned>(mqes),
                        static_cast<unsigned>(vs));
    TEST_ASSERT_GT(mqes, 0u);
    TEST_ASSERT_GT(vs, 0u);
    TEST_ASSERT_TRUE(((vs >> 16) & 0xFFFF) >= 1u);  // MJR >= 1 (NVMe 1.x+)
}

}  // namespace test_nvme

extern "C" void run_nvme_tests() {
    TEST_SECTION("NVMe");
    RUN_TEST(test_nvme::test_find_and_map);
    TEST_SUMMARY();
}
