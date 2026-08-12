/**
 * @file test/unit/test_ext2_concurrent.cpp
 * @brief F6-M6 B2: ext2 host TSAN concurrency test
 *
 * The payoff of the whole M6 host-build effort: TSan can see data races the
 * kernel's lockdep cannot (lockdep only does lock-order; it is blind to two
 * threads touching a field with NO lock). The F-DYN-COV batches 3/4 hunted the
 * inode_cache_ and block_alloc_ SMP races on QEMU with forensics (LOCKDEP held
 * stacks + RaceWatchpoint + wild traces); this test surfaces the same classes
 * of bug in milliseconds on the host.
 *
 * Two stressors, each N threads over the SAME Ext2 + RAMBlockDevice:
 *   - alloc_block / free_block hammer the block bitmap under block_alloc_lock_
 *   - concurrent lookup("/etc/motd") hammer the inode cache under
 *     inode_cache_lock_ (read-only; each ref'd result is unref'd)
 * If a lock is missing, TSan reports the racing access. Image path injected by
 * CMake (EXT2_TEST_IMG_PATH). Build: -DCINUX_HOST_TSAN=ON.
 *
 * Compile condition: -DCINUX_HOST_TEST
 */

#define TEST_FRAMEWORK_IMPL
#include "test_framework.h"

#ifdef CINUX_HOST_TEST

#    include <atomic>
#    include <cinux/expected.hpp>
#    include <cstdint>
#    include <cstring>
#    include <fstream>
#    include <iterator>
#    include <thread>
#    include <vector>

#    include "kernel/drivers/ram_block_device.hpp"
#    include "kernel/fs/file.hpp"  // inode_unref
#    include "libs/ext2/ext2.hpp"

#    ifndef EXT2_TEST_IMG_PATH
#        error "EXT2_TEST_IMG_PATH must be defined by CMake (source path to the image)"
#    endif

using cinux::drivers::RAMBlockDevice;
using cinux::fs::Ext2;
using cinux::fs::Inode;
using cinux::fs::inode_unref;

// N threads each perform 100 alloc/free cycles on the SAME Ext2 instance. All
// bitmap / superblock / BGDT updates run under block_alloc_lock_; a missing
// guard shows up as a TSan data-race report on the bitmap bytes or free-count
// fields.
TEST("ext2_concurrent: parallel alloc_block / free_block (TSAN)") {
    std::ifstream img(EXT2_TEST_IMG_PATH, std::ios::binary);
    ASSERT_TRUE(img.good());
    std::vector<char> bytes((std::istreambuf_iterator<char>(img)),
                            std::istreambuf_iterator<char>());
    ASSERT_TRUE(bytes.size() > 0 && bytes.size() % 512 == 0);
    const uint64_t nblocks = bytes.size() / 512;

    auto dev_r = RAMBlockDevice::create(nblocks);
    ASSERT_OK(dev_r);
    RAMBlockDevice dev = std::move(*dev_r);
    ASSERT_OK(dev.write_blocks(0, nblocks, bytes.data()));

    Ext2 ext2(&dev);
    ASSERT_OK(ext2.mount());

    constexpr int kThreads = 4;
    constexpr int kIters   = 100;
    std::thread   threads[kThreads];
    for (int t = 0; t < kThreads; ++t) {
        threads[t] = std::thread([&ext2]() {
            for (int i = 0; i < kIters; ++i) {
                uint32_t b = ext2.alloc_block();
                if (b != 0) {
                    ext2.free_block(b);
                }
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
}

// Regression for the shared block_buf_ race found by the original M6 TSan run:
// lookup_in_dir() now owns one KmBuf per call and reuses it across directory
// blocks, so concurrent path resolution never shares scratch storage.
TEST("ext2_concurrent: parallel lookup (TSAN)") {
    std::ifstream img(EXT2_TEST_IMG_PATH, std::ios::binary);
    ASSERT_TRUE(img.good());
    std::vector<char> bytes((std::istreambuf_iterator<char>(img)),
                            std::istreambuf_iterator<char>());
    ASSERT_TRUE(bytes.size() > 0 && bytes.size() % 512 == 0);
    const uint64_t nblocks = bytes.size() / 512;

    auto dev_r = RAMBlockDevice::create(nblocks);
    ASSERT_OK(dev_r);
    RAMBlockDevice dev = std::move(*dev_r);
    ASSERT_OK(dev.write_blocks(0, nblocks, bytes.data()));

    Ext2 ext2(&dev);
    ASSERT_OK(ext2.mount());

    constexpr int    kThreads = 4;
    constexpr int    kIters   = 200;
    std::atomic<int> failures{0};
    std::thread      threads[kThreads];
    for (int t = 0; t < kThreads; ++t) {
        threads[t] = std::thread([&ext2, &failures]() {
            for (int i = 0; i < kIters; ++i) {
                auto result = ext2.lookup("/etc/motd");
                if (!result.ok() || result.value() == nullptr) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                inode_unref(result.value());
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    ASSERT_EQ(failures.load(std::memory_order_relaxed), 0);
}

// Two independent creates in one directory used to race in two separate
// read-modify-write regions: the directory data block and the inode-table
// block. Metadata setattr uses the same whole-block inode-table RMW. A
// cache-only check can hide a lost update, so remount before checking both
// payloads and modes.
TEST("ext2_concurrent: parallel create + write + metadata survives remount (TSAN)") {
    std::ifstream img(EXT2_TEST_IMG_PATH, std::ios::binary);
    ASSERT_TRUE(img.good());
    std::vector<char> bytes((std::istreambuf_iterator<char>(img)),
                            std::istreambuf_iterator<char>());
    ASSERT_TRUE(bytes.size() > 0 && bytes.size() % 512 == 0);
    const uint64_t nblocks = bytes.size() / 512;

    auto dev_r = RAMBlockDevice::create(nblocks);
    ASSERT_OK(dev_r);
    RAMBlockDevice dev = std::move(*dev_r);
    ASSERT_OK(dev.write_blocks(0, nblocks, bytes.data()));

    constexpr int     kThreads           = 2;
    const char*       names[kThreads]    = {"parallel-a", "parallel-b"};
    const char*       payloads[kThreads] = {"payload-from-a", "payload-from-b"};
    const uint32_t    modes[kThreads]    = {0601, 0642};
    std::atomic<int>  failures{0};
    std::atomic<int>  ready{0};
    std::atomic<bool> start{false};
    std::atomic<int>  metadata_ready{0};
    std::atomic<bool> metadata_start{false};

    {
        Ext2 ext2(&dev);
        ASSERT_OK(ext2.mount());

        std::thread threads[kThreads];
        for (int t = 0; t < kThreads; ++t) {
            threads[t] = std::thread([&ext2, &failures, &names, &payloads, &modes, &ready, &start,
                                      &metadata_ready, &metadata_start, t]() {
                auto  root_r = ext2.lookup("/");
                auto* root   = root_r.ok() ? root_r.value() : nullptr;
                ready.fetch_add(1, std::memory_order_release);
                while (!start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }

                cinux::fs::Inode* file = nullptr;
                if (root == nullptr) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                } else {
                    auto created = root->ops->create(root, names[t],
                                                     static_cast<uint32_t>(std::strlen(names[t])));
                    inode_unref(root);
                    if (!created.ok() || created.value() == nullptr) {
                        failures.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        file = created.value();
                        auto written =
                            file->ops->write(file, 0, payloads[t], std::strlen(payloads[t]));
                        if (!written.ok() ||
                            written.value() != static_cast<int64_t>(std::strlen(payloads[t]))) {
                            failures.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }

                metadata_ready.fetch_add(1, std::memory_order_release);
                while (!metadata_start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                if (file != nullptr) {
                    auto chmod_result = file->ops->chmod(file, modes[t]);
                    if (!chmod_result.ok()) {
                        failures.fetch_add(1, std::memory_order_relaxed);
                    }
                    inode_unref(file);
                }
            });
        }
        while (ready.load(std::memory_order_acquire) != kThreads) {
            std::this_thread::yield();
        }
        start.store(true, std::memory_order_release);
        while (metadata_ready.load(std::memory_order_acquire) != kThreads) {
            std::this_thread::yield();
        }
        metadata_start.store(true, std::memory_order_release);
        for (auto& thread : threads) {
            thread.join();
        }
        ASSERT_EQ(failures.load(std::memory_order_relaxed), 0);
    }

    Ext2 remounted(&dev);
    ASSERT_OK(remounted.mount());
    for (int t = 0; t < kThreads; ++t) {
        auto file_r = remounted.lookup(names[t]);
        ASSERT_OK(file_r);
        auto* file = file_r.value();
        char  buf[32]{};
        auto  read = file->ops->read(file, 0, buf, sizeof(buf));
        ASSERT_OK(read);
        ASSERT_EQ(read.value(), static_cast<int64_t>(std::strlen(payloads[t])));
        ASSERT_EQ(std::memcmp(buf, payloads[t], std::strlen(payloads[t])), 0);
        ASSERT_EQ(file->mode & 0777U, modes[t]);
        inode_unref(file);
    }
}

// A live cached inode can be refreshed after setattr marks it stale. Exercise
// that refresh against file read/stat so TSan verifies every cached disk_inode
// reader shares the same outer mutation lock as refresh and write/truncate.
TEST("ext2_concurrent: cached inode refresh vs read + stat (TSAN)") {
    std::ifstream img(EXT2_TEST_IMG_PATH, std::ios::binary);
    ASSERT_TRUE(img.good());
    std::vector<char> bytes((std::istreambuf_iterator<char>(img)),
                            std::istreambuf_iterator<char>());
    ASSERT_TRUE(bytes.size() > 0 && bytes.size() % 512 == 0);
    const uint64_t nblocks = bytes.size() / 512;

    auto dev_r = RAMBlockDevice::create(nblocks);
    ASSERT_OK(dev_r);
    RAMBlockDevice dev = std::move(*dev_r);
    ASSERT_OK(dev.write_blocks(0, nblocks, bytes.data()));

    Ext2 ext2(&dev);
    ASSERT_OK(ext2.mount());
    auto writer_r = ext2.lookup("/etc/motd");
    auto reader_r = ext2.lookup("/etc/motd");
    ASSERT_OK(writer_r);
    ASSERT_OK(reader_r);
    Inode* writer_inode = writer_r.value();
    Inode* reader_inode = reader_r.value();

    constexpr int     kIters = 200;
    std::atomic<int>  failures{0};
    std::atomic<int>  ready{0};
    std::atomic<bool> start{false};

    std::thread writer([&]() {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (int i = 0; i < kIters; ++i) {
            const uint32_t mode = (i & 1) == 0 ? 0640 : 0604;
            if (!writer_inode->ops->chmod(writer_inode, mode).ok()) {
                failures.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            auto refreshed = ext2.lookup("/etc/motd");
            if (!refreshed.ok() || refreshed.value() == nullptr) {
                failures.fetch_add(1, std::memory_order_relaxed);
            } else {
                inode_unref(refreshed.value());
            }
        }
    });

    std::thread reader([&]() {
        ready.fetch_add(1, std::memory_order_release);
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (int i = 0; i < kIters; ++i) {
            cinux::fs::stat st{};
            char            buf[32]{};
            auto            stat_result = reader_inode->ops->stat(reader_inode, &st);
            auto read_result = reader_inode->ops->read(reader_inode, 0, buf, sizeof(buf));
            if (!stat_result.ok() || !read_result.ok() || read_result.value() <= 0) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    while (ready.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    writer.join();
    reader.join();

    ASSERT_EQ(failures.load(std::memory_order_relaxed), 0);
    ASSERT_EQ(writer_inode->mode & 0777U, 0604U);
    inode_unref(writer_inode);
    inode_unref(reader_inode);
}

int main() {
    RUN_ALL_TESTS();
    return _tests_failed > 0 ? 1 : 0;
}

#endif  // CINUX_HOST_TEST
