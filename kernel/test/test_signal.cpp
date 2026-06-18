/**
 * @file kernel/test/test_signal.cpp
 * @brief QEMU in-kernel tests for the signal data layer (F3-M1 batch 1)
 *
 * Covers the bitmask signal-set operations, the default-disposition table,
 * the uncatchable predicate, the validity check, and that the per-Task
 * signal fields default to a clean state (fork relies on this).  Delivery
 * and the syscall surface arrive in later batches.
 */

#include <stdint.h>

#include "big_kernel_test.h"
#include "kernel/proc/process.hpp"
#include "kernel/proc/signal.hpp"

using namespace cinux::proc;

// ============================================================
// Signal-set bitmask operations
// ============================================================

namespace test_sigset {

void test_set_add_del_member() {
    SigSet s = 0;
    sig_set_add(s, Signal::kSigint);
    sig_set_add(s, Signal::kSigterm);
    TEST_ASSERT_TRUE(sig_is_member(s, Signal::kSigint));
    TEST_ASSERT_TRUE(sig_is_member(s, Signal::kSigterm));
    TEST_ASSERT_FALSE(sig_is_member(s, Signal::kSighup));

    sig_set_del(s, Signal::kSigint);
    TEST_ASSERT_FALSE(sig_is_member(s, Signal::kSigint));
    TEST_ASSERT_TRUE(sig_is_member(s, Signal::kSigterm));
}

void test_make_mask_bit_position() {
    // bit n <=> signal n: signal 9 (SIGKILL) sets bit 9, signal 1 sets bit 1.
    TEST_ASSERT_EQ(sig_make_mask(Signal::kSigkill), SigSet{1} << 9);
    TEST_ASSERT_EQ(sig_make_mask(Signal::kSighup), SigSet{1} << 1);
}

}  // namespace test_sigset

// ============================================================
// Default disposition table
// ============================================================

namespace test_sig_default {

void test_default_actions() {
    TEST_ASSERT_EQ(signal_default_action(Signal::kSigterm), SigDefault::kTerminate);
    TEST_ASSERT_EQ(signal_default_action(Signal::kSighup), SigDefault::kTerminate);
    TEST_ASSERT_EQ(signal_default_action(Signal::kSigpipe), SigDefault::kTerminate);

    TEST_ASSERT_EQ(signal_default_action(Signal::kSigquit), SigDefault::kCoreDump);
    TEST_ASSERT_EQ(signal_default_action(Signal::kSigsegv), SigDefault::kCoreDump);
    TEST_ASSERT_EQ(signal_default_action(Signal::kSigfpe), SigDefault::kCoreDump);

    TEST_ASSERT_EQ(signal_default_action(Signal::kSigchld), SigDefault::kIgnore);

    TEST_ASSERT_EQ(signal_default_action(Signal::kSigcont), SigDefault::kContinue);

    TEST_ASSERT_EQ(signal_default_action(Signal::kSigstop), SigDefault::kStop);
    TEST_ASSERT_EQ(signal_default_action(Signal::kSigtstp), SigDefault::kStop);
}

}  // namespace test_sig_default

// ============================================================
// Uncatchable / validity
// ============================================================

namespace test_sig_meta {

void test_uncatchable() {
    TEST_ASSERT_TRUE(signal_is_uncatchable(Signal::kSigkill));
    TEST_ASSERT_TRUE(signal_is_uncatchable(Signal::kSigstop));
    TEST_ASSERT_FALSE(signal_is_uncatchable(Signal::kSigterm));
    TEST_ASSERT_FALSE(signal_is_uncatchable(Signal::kSigint));
}

void test_valid() {
    TEST_ASSERT_TRUE(signal_valid(1));
    TEST_ASSERT_TRUE(signal_valid(static_cast<int>(Signal::kSigtou)));
    TEST_ASSERT_FALSE(signal_valid(0));
    TEST_ASSERT_FALSE(signal_valid(static_cast<int>(Signal::kSigtou) + 1));
}

}  // namespace test_sig_meta

// ============================================================
// SigAction / Task wiring
// ============================================================

namespace test_sig_state {

void test_sigaction_default_ctor() {
    SigAction a;
    TEST_ASSERT_EQ(a.type, HandlerType::kDefault);
    TEST_ASSERT_EQ(a.handler_addr, uint64_t{0});
    TEST_ASSERT_EQ(a.sa_mask, SigSet{0});
}

void test_task_has_signal_fields() {
    // A value-initialised Task starts with no pending/blocked signals and
    // every disposition at default -- fork relies on this baseline.
    Task t{};
    TEST_ASSERT_EQ(t.sig_pending, SigSet{0});
    TEST_ASSERT_EQ(t.sig_blocked, SigSet{0});
    TEST_ASSERT_EQ(t.sig_actions[static_cast<int>(Signal::kSigterm)].type, HandlerType::kDefault);
}

}  // namespace test_sig_state

// ============================================================
// Entry point
// ============================================================

extern "C" void run_signal_tests() {
    TEST_SECTION("Signal Tests (F3-M1-1)");

    RUN_TEST(test_sigset::test_set_add_del_member);
    RUN_TEST(test_sigset::test_make_mask_bit_position);
    RUN_TEST(test_sig_default::test_default_actions);
    RUN_TEST(test_sig_meta::test_uncatchable);
    RUN_TEST(test_sig_meta::test_valid);
    RUN_TEST(test_sig_state::test_sigaction_default_ctor);
    RUN_TEST(test_sig_state::test_task_has_signal_fields);

    TEST_SUMMARY();
}
