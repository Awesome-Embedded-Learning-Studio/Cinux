/**
 * @file kernel/test/test_process_group.cpp
 * @brief QEMU in-kernel tests for process-group / session (F3-M3 batch 1)
 *
 * Batch 1 only: verifies inherit_process_identity() -- the pure rule that
 * fork() and clone() use to derive a child's pgid / sid / session_leader /
 * controlling_tty.  Two paths are exercised:
 *   - root fork (parent->pgid == 0, i.e. a kernel/bootstrap task): the child
 *     founds a brand-new process group AND session and leads both
 *   - normal fork: the child inherits the parent's group, session, leader
 *     pointer, and controlling terminal
 *
 * setpgid / getsid / killpg and the syscalls land in batch 2/3.
 *
 * The function is a pure transformation on Task fields, so these tests need
 * no scheduler loop and no installed current task.
 */

#include <stdint.h>

#include "big_kernel_test.h"
#include "kernel/proc/process.hpp"
#include "kernel/proc/process_internal.hpp"

using cinux::proc::Task;
using cinux::proc::inherit_process_identity;

// ============================================================
// Path 1: root fork -- child founds its own group and session
// ============================================================

namespace test_pgrp_root_fork {

void test_root_fork_founds_new_group_and_session() {
    // The parent is a kernel/bootstrap task with no group (pgid == 0).
    Task parent{};
    parent.pgid            = 0;
    parent.sid             = 0;
    parent.session_leader  = nullptr;
    parent.controlling_tty = -1;

    Task child{};
    inherit_process_identity(&child, &parent, /*child_pid=*/1);

    // The child becomes its own group leader and session leader.
    TEST_ASSERT_EQ(child.pgid, 1);
    TEST_ASSERT_EQ(child.sid, 1);
    TEST_ASSERT_EQ(child.session_leader, &child);
    TEST_ASSERT_EQ(child.controlling_tty, -1);  // none to inherit
}

void test_root_fork_ignores_stale_parent_session() {
    // Even if the root parent had garbage sid (it should not, but the rule
    // keys off pgid == 0), the child still founds a fresh session.
    Task parent{};
    parent.pgid = 0;
    parent.sid  = 999;  // ignored on the root path

    Task child{};
    inherit_process_identity(&child, &parent, 5);

    TEST_ASSERT_EQ(child.pgid, 5);
    TEST_ASSERT_EQ(child.sid, 5);
    TEST_ASSERT_EQ(child.session_leader, &child);
}

}  // namespace test_pgrp_root_fork

// ============================================================
// Path 2: normal fork -- child inherits the parent's membership
// ============================================================

namespace test_pgrp_inherit {

void test_child_inherits_parent_group_and_session() {
    // A normal process that already belongs to group 1 / session 1 (led by
    // `leader`), and holds controlling terminal index 3.
    Task leader{};
    leader.pgid            = 1;
    leader.sid             = 1;
    leader.session_leader  = &leader;
    leader.controlling_tty = 3;

    Task child{};
    inherit_process_identity(&child, &leader, /*child_pid=*/2);

    TEST_ASSERT_EQ(child.pgid, 1);                  // same group as parent
    TEST_ASSERT_EQ(child.sid, 1);                   // same session
    TEST_ASSERT_EQ(child.session_leader, &leader);  // leader pointer shared
    TEST_ASSERT_EQ(child.controlling_tty, 3);       // tty inherited
}

void test_inherit_preserves_arbitrary_pgid_sid() {
    // setpgid() (batch 2) will move a process into an arbitrary group; a fork
    // from it must carry that group forward verbatim.
    Task parent{};
    parent.pgid            = 42;
    parent.sid             = 17;
    parent.controlling_tty = 7;

    Task child{};
    inherit_process_identity(&child, &parent, 99);

    TEST_ASSERT_EQ(child.pgid, 42);
    TEST_ASSERT_EQ(child.sid, 17);
    TEST_ASSERT_EQ(child.controlling_tty, 7);
}

}  // namespace test_pgrp_inherit

// ============================================================
// End-to-end chain: kernel_init -> init -> grandchild
// ============================================================

namespace test_pgrp_chain {

void test_kernel_init_to_init_to_grandchild() {
    // kernel_init_thread (pid 0, no group) forks the first user process.
    Task kinit{};
    kinit.pgid = 0;

    Task init_t{};
    inherit_process_identity(&init_t, &kinit, /*child_pid=*/1);
    TEST_ASSERT_EQ(init_t.pgid, 1);  // init founds group 1
    TEST_ASSERT_EQ(init_t.sid, 1);
    TEST_ASSERT_EQ(init_t.session_leader, &init_t);

    // init forks a child, which must stay in init's group/session.
    Task grandchild{};
    inherit_process_identity(&grandchild, &init_t, /*child_pid=*/2);
    TEST_ASSERT_EQ(grandchild.pgid, 1);  // inherited, not refounded
    TEST_ASSERT_EQ(grandchild.sid, 1);
    TEST_ASSERT_EQ(grandchild.session_leader, &init_t);
}

}  // namespace test_pgrp_chain

extern "C" void run_process_group_tests() {
    TEST_SECTION("Process Group Tests (F3-M3-1)");

    RUN_TEST(test_pgrp_root_fork::test_root_fork_founds_new_group_and_session);
    RUN_TEST(test_pgrp_root_fork::test_root_fork_ignores_stale_parent_session);

    RUN_TEST(test_pgrp_inherit::test_child_inherits_parent_group_and_session);
    RUN_TEST(test_pgrp_inherit::test_inherit_preserves_arbitrary_pgid_sid);

    RUN_TEST(test_pgrp_chain::test_kernel_init_to_init_to_grandchild);

    TEST_SUMMARY();
}
