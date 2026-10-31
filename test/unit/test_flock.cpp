#include "cinux/result.hpp"
#include "framework.hpp"
#include "kernel/fs/file_lock.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/fs/vfs.hpp"
#include "test_assert.hpp"

namespace {

using cinux::base::KernelError;
using cinux::base::Result;
using cinux::fs::FileLocks;
using cinux::fs::FsNode;
using cinux::fs::OpenFile;

/// One fake backend identity, so every open names a node the same way
/// the real backends do: which instance, which inode number.
auto* const kBackend = reinterpret_cast<void*>(0x10);

OpenFile make_open(unsigned long ino) {
    FsNode node{};
    node.ino = ino;
    return {nullptr, kBackend, node};
}

bool granted(const Result<void>& attempt) {
    return attempt.ok();
}

bool refused(const Result<void>& attempt) {
    return !attempt.ok() && attempt.error() == KernelError::kWouldBlock;
}

int g_previous_calls = 0;

void count_previous_call([[maybe_unused]] const OpenFile* file) {
    ++g_previous_calls;
}

TEST("flock: shared readers coexist, exclusive conflicts, unlock hands the node over") {
    FileLocks& locks  = FileLocks::self();
    OpenFile   first  = make_open(1);
    OpenFile   second = make_open(1);
    ASSERT_TRUE(granted(locks.attempt(first, cinux::fs::kFlockSh)));
    ASSERT_TRUE(granted(locks.attempt(second, cinux::fs::kFlockSh)));
    ASSERT_TRUE(refused(locks.attempt(first, cinux::fs::kFlockEx)));
    ASSERT_TRUE(refused(locks.attempt(second, cinux::fs::kFlockEx)));
    ASSERT_TRUE(granted(locks.attempt(first, cinux::fs::kFlockUn)));
    ASSERT_TRUE(granted(locks.attempt(second, cinux::fs::kFlockUn)));
    ASSERT_TRUE(granted(locks.attempt(first, cinux::fs::kFlockEx)));
    ASSERT_TRUE(refused(locks.attempt(second, cinux::fs::kFlockSh)));
}

TEST("flock: one open re-locks in place, upgrading and downgrading without blocking") {
    FileLocks& locks = FileLocks::self();
    OpenFile   open  = make_open(2);
    ASSERT_TRUE(granted(locks.attempt(open, cinux::fs::kFlockSh)));
    ASSERT_TRUE(granted(locks.attempt(open, cinux::fs::kFlockEx)));
    ASSERT_TRUE(granted(locks.attempt(open, cinux::fs::kFlockSh)));
    OpenFile rival = make_open(2);
    ASSERT_TRUE(refused(locks.attempt(rival, cinux::fs::kFlockEx)));
    ASSERT_TRUE(granted(locks.attempt(open, cinux::fs::kFlockEx)));
    ASSERT_TRUE(refused(locks.attempt(rival, cinux::fs::kFlockSh)));
}

TEST("flock: different nodes never fight, and bad operation words are refused") {
    FileLocks& locks = FileLocks::self();
    OpenFile   one   = make_open(3);
    OpenFile   two   = make_open(4);
    ASSERT_TRUE(granted(locks.attempt(one, cinux::fs::kFlockEx)));
    ASSERT_TRUE(granted(locks.attempt(two, cinux::fs::kFlockEx)));
    const Result<void> kBoth = locks.attempt(one, cinux::fs::kFlockSh | cinux::fs::kFlockEx);
    ASSERT_TRUE(!kBoth.ok() && kBoth.error() == KernelError::kInvalidArgument);
    const Result<void> kEmpty = locks.attempt(one, 0);
    ASSERT_TRUE(!kEmpty.ok() && kEmpty.error() == KernelError::kInvalidArgument);
    ASSERT_TRUE(granted(locks.attempt(one, cinux::fs::kFlockUn)));
    ASSERT_TRUE(granted(locks.attempt(two, cinux::fs::kFlockUn)));
}

TEST("flock: the last close drops the open's lock without LOCK_UN") {
    FileLocks& locks = FileLocks::self();
    {
        OpenFile holder = make_open(5);
        ASSERT_TRUE(granted(locks.attempt(holder, cinux::fs::kFlockEx)));
    }
    OpenFile heir = make_open(5);
    ASSERT_TRUE(granted(locks.attempt(heir, cinux::fs::kFlockEx)));
    ASSERT_TRUE(granted(locks.attempt(heir, cinux::fs::kFlockUn)));
}

TEST("flock: the final-close chain keeps an observer installed before the lock") {
    FileLocks& locks = FileLocks::self();
    g_previous_calls = 0;
    {
        OpenFile watched = make_open(6);
        watched.on_last_close.store(&count_previous_call);
        ASSERT_TRUE(granted(locks.attempt(watched, cinux::fs::kFlockEx)));
    }
    ASSERT_EQ(g_previous_calls, 1);
    OpenFile next = make_open(6);
    ASSERT_TRUE(granted(locks.attempt(next, cinux::fs::kFlockEx)));
    ASSERT_TRUE(granted(locks.attempt(next, cinux::fs::kFlockUn)));
}

}  // namespace

int main() {
    return cinux::test::RunAll();
}
