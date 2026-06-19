/**
 * @file kernel/test/test_apic.cpp
 * @brief Local APIC driver mock tests (F4-M2)
 *
 * Uses a plain RAM page as a fake 4 KB MMIO window to verify the driver's
 * register access and enable/eoi logic without touching real hardware.  The
 * actual PIC->APIC switch (and real LAPIC use) lands in M2-3.
 */

#include <stdint.h>

#include "big_kernel_test.h"
#include "kernel/drivers/apic/local_apic.hpp"

using cinux::drivers::apic::LocalAPIC;
using cinux::drivers::apic::kRegEoi;
using cinux::drivers::apic::kRegErrorStatus;
using cinux::drivers::apic::kRegId;
using cinux::drivers::apic::kRegSpurious;
using cinux::drivers::apic::kRegTaskPriority;
using cinux::drivers::apic::kSvrEnable;

namespace {

/// A mock 4 KB MMIO window backed by plain RAM (volatile so writes through the
/// driver's volatile pointer are observable on re-read).
struct MockMmio {
    static constexpr size_t kWords = 256;  ///< covers offsets up to 0x3FF
    volatile uint32_t       words[kWords];

    void clear() {
        for (auto& w : words) {
            w = 0;
        }
    }
    volatile uint32_t* base() { return words; }
};

}  // namespace

namespace test_local_apic {

void test_read_write_roundtrip() {
    MockMmio mmio;
    mmio.clear();
    LocalAPIC lapic;
    lapic.bind(mmio.base());
    lapic.write(kRegTaskPriority, 0x12345678);
    TEST_ASSERT_TRUE(lapic.read(kRegTaskPriority) == 0x12345678);
}

void test_enable_sets_svr() {
    MockMmio mmio;
    mmio.clear();
    LocalAPIC lapic;
    lapic.bind(mmio.base());
    lapic.enable(0xFF);
    TEST_ASSERT_TRUE((mmio.words[kRegSpurious / 4] & 0xFF) == 0xFF);
    TEST_ASSERT_TRUE((mmio.words[kRegSpurious / 4] & kSvrEnable) != 0);
}

void test_disable_clears_enable() {
    MockMmio mmio;
    mmio.clear();
    LocalAPIC lapic;
    lapic.bind(mmio.base());
    lapic.enable(0x20);
    lapic.disable();
    TEST_ASSERT_TRUE((mmio.words[kRegSpurious / 4] & kSvrEnable) == 0);
}

void test_eoi_writes_zero() {
    MockMmio mmio;
    mmio.clear();
    mmio.words[kRegEoi / 4] = 0xDEADBEEF;
    LocalAPIC lapic;
    lapic.bind(mmio.base());
    lapic.eoi();
    TEST_ASSERT_TRUE(mmio.words[kRegEoi / 4] == 0);
}

void test_id_decodes_bits_24_31() {
    MockMmio mmio;
    mmio.clear();
    mmio.words[kRegId / 4] = 0x05000000;  // APIC ID 5 in bits 24-31
    LocalAPIC lapic;
    lapic.bind(mmio.base());
    TEST_ASSERT_TRUE(lapic.id() == 5);
}

void test_clear_error() {
    MockMmio mmio;
    mmio.clear();
    mmio.words[kRegErrorStatus / 4] = 0xAB;
    LocalAPIC lapic;
    lapic.bind(mmio.base());
    lapic.clear_error();
    TEST_ASSERT_TRUE(mmio.words[kRegErrorStatus / 4] == 0);
}

}  // namespace test_local_apic

extern "C" void run_apic_tests() {
    TEST_SECTION("APIC (F4-M2)");
    RUN_TEST(test_local_apic::test_read_write_roundtrip);
    RUN_TEST(test_local_apic::test_enable_sets_svr);
    RUN_TEST(test_local_apic::test_disable_clears_enable);
    RUN_TEST(test_local_apic::test_eoi_writes_zero);
    RUN_TEST(test_local_apic::test_id_decodes_bits_24_31);
    RUN_TEST(test_local_apic::test_clear_error);
    TEST_SUMMARY();
}
