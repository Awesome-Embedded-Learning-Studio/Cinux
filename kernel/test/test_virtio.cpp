/**
 * @file kernel/test/test_virtio.cpp
 * @brief QEMU in-kernel integration tests for the VirtIO transport (F5-M2 batch 1)
 *
 * Runs inside the big kernel test suite.  PCI-enumerates a VirtIO Block device
 * (vendor 0x1AF4 + device 0x1001/0x1042); if none is present the test SKIPS.
 * Under run-kernel-test-all (-device virtio-blk-pci ...) it exercises the
 * batch-1 bring-up: PCI find + capability parse (common/notify/isr/device_cfg
 * all located) + 64-bit feature negotiation (VERSION_1 accepted) + split
 * virtqueue configuration (queue 0 registered + enabled, notify_off read).
 *
 * Round-trip (submit/kick/wait consuming a real blk request) is deferred to
 * batch 2 -- it needs the virtio-blk 3-desc request format (header + data +
 * status), which VirtQueue::submit_one's single-desc path cannot express yet.
 *
 * Preconditions: VMM initialised (g_vmm.init in main_test.cpp) for BAR map.
 */

#include <stdint.h>

#include "big_kernel_test.h"
#include "kernel/drivers/pci/pci.hpp"
#include "kernel/drivers/virtio/virtio.hpp"
#include "kernel/drivers/virtio/virtqueue.hpp"
#include "kernel/lib/kprintf.hpp"

using cinux::drivers::pci::PCIDevice;
using cinux::drivers::pci::PCI;
using namespace cinux::drivers;  // so `pci::` resolves to cinux::drivers::pci
using namespace cinux::drivers::virtio;

namespace test_virtio {

// ============================================================
// Test 1: PCI find + cap parse + feature negotiation + queue config
// ============================================================

void test_transport_bringup() {
    PCI pci;
    pci.init();

    PCIDevice dev{};
    if (!pci.find_virtio_block(dev)) {
        cinux::lib::kprintf("[VirtIO] no device present -- skipping transport test\n");
        return;  // no -device virtio-blk-pci: counts as a pass (skip)
    }

    // Vendor match (find_virtio_block already filtered, but assert the decoded
    // ID to prove the matcher didn't bind a non-VirtIO device by mistake).
    TEST_ASSERT_EQ(dev.vendor_id, pci::VirtioPci::VENDOR);  // 0x1AF4

    VirtIODevice            vdev;
    cinux::lib::ErrorOr<void> r = vdev.init(dev);
    TEST_ASSERT_TRUE(r.ok());
    TEST_ASSERT_TRUE(vdev.present());

    // Capability parse: all four modern-transport blocks located.  A missed
    // block means the cap-list walk dropped an entry or the BAR mapping
    // failed; the transport cannot be driven, so fail loud.
    const VirtioCapLocations& c = vdev.caps();
    TEST_ASSERT_TRUE(c.found_common);
    TEST_ASSERT_TRUE(c.found_notify);
    TEST_ASSERT_TRUE(c.found_isr);
    TEST_ASSERT_TRUE(c.found_device);

    // Feature negotiation: the device accepts VirtIO 1.0 (modern).  A
    // legacy-only device would reject FEATURES_OK here.
    cinux::lib::ErrorOr<uint64_t> fr = vdev.negotiate_features(Feature::VERSION_1);
    TEST_ASSERT_TRUE(fr.ok());
    TEST_ASSERT_TRUE((fr.value() & Feature::VERSION_1) != 0);

    // Split virtqueue configuration: queue 0, 64 entries.  init() allocates +
    // zeroes the three DMA tables, registers their phys via setup_queue, reads
    // notify_off, and enables the queue -- all before DRIVER_OK, which is legal
    // (queue_enable only requires FEATURES_OK, set during negotiate_features).
    VirtQueue                   vq;
    cinux::lib::ErrorOr<void> qr = vq.init(&vdev, 0, 64);
    TEST_ASSERT_TRUE(qr.ok());
    TEST_ASSERT_EQ(vq.size(), 64u);

    // DRIVER_OK -- the device enters live state and may now process avail
    // ring entries on the next kick (batch 2 round-trip).
    vdev.set_status(Status::DRIVER_OK);
    TEST_ASSERT_TRUE((vdev.status() & Status::DRIVER_OK) != 0);

    cinux::lib::kprintf(
        "[VirtIO] transport OK: negotiated=0x%llx status=0x%x common=BAR%u+0x%x "
        "notify=BAR%u+0x%x mult=%u qsz=64 notify_off=%u\n",
        static_cast<unsigned long long>(fr.value()), vdev.status(), c.common_bar, c.common_off,
        c.notify_bar, c.notify_off, c.notify_off_multiplier, vq.last_used_idx());
}

}  // namespace test_virtio

extern "C" void run_virtio_tests() {
    TEST_SECTION("VirtIO");
    RUN_TEST(test_virtio::test_transport_bringup);
    TEST_SUMMARY();
}
