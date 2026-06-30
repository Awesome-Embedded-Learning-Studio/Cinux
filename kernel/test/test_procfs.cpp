/**
 * @file kernel/test/test_procfs.cpp
 * @brief In-kernel tests for ProcFS (F6-M2)
 *
 * Runs inside QEMU via run-kernel-test.  procfs.cpp reads the live task
 * registry (no host injection seam), so ProcFS is exercised here under the
 * freestanding build rather than in test/unit.  A stack Task is registered with
 * a known free PID so the lookup-found / lookup-NotFound assertions are
 * deterministic regardless of tasks left behind by earlier test sections
 * (mirrors the F3-M4 stack-Task technique).
 *
 * Batch 1: mount, root + per-PID directory lookup, directory stat, root readdir
 * (dot/dotdot only).  Later batches extend this file (readdir PID enumeration,
 * stat/cmdline pseudo-file reads).
 */

#include <stdint.h>

#include "kernel/fs/procfs.hpp"
#include "kernel/lib/string.hpp"    // utoa
#include "kernel/proc/process.hpp"  // Task
#include "kernel/proc/signal.hpp"   // signal_register/unregister/find_by_pid
#include "kernel/test/big_kernel_test.h"

using namespace cinux::fs;
using cinux::lib::kprintf;
using cinux::proc::Task;
using cinux::proc::signal_find_task_by_pid;
using cinux::proc::signal_register_task;
using cinux::proc::signal_unregister_task;

namespace {

/// Pick a PID in [200, 256] that is currently free in the registry.  The test
/// kernel's own main thread is not registered (it runs on the boot stack), but
/// earlier sections may have left tasks behind; starting from a high band keeps
/// the lookup-found / NotFound assertions deterministic without assuming an
/// empty registry.
int pick_free_pid() {
    for (int p = 200; p <= kProcPidMax; ++p) {
        if (signal_find_task_by_pid(p) == nullptr) {
            return p;
        }
    }
    return 200;
}

/// Write the decimal of @p pid into @p buf (enough for any value <= 256).
void pid_to_path(char* buf, int pid) {
    utoa(buf, static_cast<uint32_t>(pid));
}

}  // namespace

namespace test_procfs {

// ---------- mount + root ----------

void test_mount_and_root() {
    ProcFs pf;
    TEST_ASSERT_TRUE(pf.mount().ok());
    Inode* root = lookup_or_null(&pf, "");
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQ(static_cast<int>(root->type), static_cast<int>(InodeType::Directory));
    // mount() is idempotent (mirrors DevFs).
    TEST_ASSERT_TRUE(pf.mount().ok());
}

void test_root_stat_is_directory() {
    ProcFs pf;
    TEST_ASSERT_TRUE(pf.mount().ok());
    Inode*      root = lookup_or_null(&pf, "");
    struct stat st;
    TEST_ASSERT_TRUE(root->ops->stat(root, &st).ok());
    TEST_ASSERT_EQ(st.st_mode, static_cast<uint32_t>(kProcSIfDir | 0555));
    TEST_ASSERT_EQ(st.st_nlink, 2u);
}

void test_root_readdir_dot_dotdot() {
    ProcFs pf;
    TEST_ASSERT_TRUE(pf.mount().ok());
    Inode* root = lookup_or_null(&pf, "");
    char   name[PROCFS_NAME_MAX];

    TEST_ASSERT_EQ(readdir_or_neg1(root, 0, name, sizeof(name)), 1);
    TEST_ASSERT_EQ(name[0], '.');
    TEST_ASSERT_EQ(name[1], '\0');

    TEST_ASSERT_EQ(readdir_or_neg1(root, 1, name, sizeof(name)), 1);
    TEST_ASSERT_EQ(name[0], '.');
    TEST_ASSERT_EQ(name[1], '.');
}

void test_root_readdir_lists_live_pid() {
    ProcFs pf;
    TEST_ASSERT_TRUE(pf.mount().ok());

    int  pid = pick_free_pid();
    Task t{};
    t.pid  = pid;
    t.name = "procfs_test";
    signal_register_task(&t);

    Inode* root = lookup_or_null(&pf, "");
    char   name[PROCFS_NAME_MAX];
    char   expected[16];
    pid_to_path(expected, pid);

    // Walk past "." / ".." (indices 0/1) and find our PID among the live set.
    bool found = false;
    for (uint64_t idx = 2; idx < 64; ++idx) {
        int64_t r = readdir_or_neg1(root, idx, name, sizeof(name));
        if (r != 1) {
            break;  // end of directory
        }
        if (strcmp(name, expected) == 0) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);

    // After unregister, the PID no longer appears.
    signal_unregister_task(&t);
    found = false;
    for (uint64_t idx = 2; idx < 64; ++idx) {
        int64_t r = readdir_or_neg1(root, idx, name, sizeof(name));
        if (r != 1) {
            break;
        }
        if (strcmp(name, expected) == 0) {
            found = true;
            break;
        }
    }
    TEST_ASSERT_FALSE(found);
}

// ---------- per-PID directory lookup ----------

void test_lookup_pid_dir_live_then_dead() {
    ProcFs pf;
    TEST_ASSERT_TRUE(pf.mount().ok());

    int  pid = pick_free_pid();
    Task t{};
    t.pid  = pid;
    t.name = "procfs_test";
    signal_register_task(&t);

    char path[16];
    pid_to_path(path, pid);

    // A live PID resolves to a per-process directory.
    Inode* dir = lookup_or_null(&pf, path);
    TEST_ASSERT_NOT_NULL(dir);
    TEST_ASSERT_EQ(static_cast<int>(dir->type), static_cast<int>(InodeType::Directory));

    struct stat st;
    TEST_ASSERT_TRUE(dir->ops->stat(dir, &st).ok());
    TEST_ASSERT_EQ(st.st_mode, static_cast<uint32_t>(kProcSIfDir | 0555));
    TEST_ASSERT_EQ(st.st_ino, static_cast<uint64_t>(pid));

    // Once unregistered, the same PID is gone (Linux only exposes live tasks).
    signal_unregister_task(&t);
    TEST_ASSERT_NULL(lookup_or_null(&pf, path));
}

void test_lookup_rejects_bad_paths() {
    ProcFs pf;
    TEST_ASSERT_TRUE(pf.mount().ok());

    TEST_ASSERT_NULL(lookup_or_null(&pf, "notapid"));  // non-numeric
    TEST_ASSERT_NULL(lookup_or_null(&pf, "0"));        // PID 0 is invalid
    TEST_ASSERT_NULL(lookup_or_null(&pf, "99999"));    // out of the allocator range
    TEST_ASSERT_NULL(lookup_or_null(&pf, nullptr));    // null path
}

}  // namespace test_procfs

// ============================================================
// Entry point
// ============================================================

extern "C" void run_procfs_tests() {
    TEST_SECTION("ProcFS (F6-M2)");
    RUN_TEST(test_procfs::test_mount_and_root);
    RUN_TEST(test_procfs::test_root_stat_is_directory);
    RUN_TEST(test_procfs::test_root_readdir_dot_dotdot);
    RUN_TEST(test_procfs::test_root_readdir_lists_live_pid);
    RUN_TEST(test_procfs::test_lookup_pid_dir_live_then_dead);
    RUN_TEST(test_procfs::test_lookup_rejects_bad_paths);
    TEST_SUMMARY();
}
