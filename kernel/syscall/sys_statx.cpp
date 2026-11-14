/**
 * @file    sys_statx.cpp
 * @brief   The statx() body: one node's shape in the fixed-width record
 *          every port shares.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.3.0
 * @ingroup kernel_syscall
 * @copyright Copyright (c) 2026
 */

#include "kernel/boot/print.hpp"
#include "kernel/syscall/sys_statx.hpp"

#include <cstdint>

#include "cinux/result.hpp"
#include "cinux/shared.hpp"
#include "kernel/fs/fs_config.hpp"
#include "kernel/fs/kernel_vfs.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/syscall/syscall_internal.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::syscall {

namespace {

/// Which parts of the record carry real answers.
constexpr std::uint32_t kStatxBasicStats = 0x7FFU;  ///< mode, nlink, uid, gid, atime, mtime, ctime, ino, size, blocks
constexpr std::uint32_t kAtFdCwd         = 0xFFFFFF9CU;

/// The statx record, field for field as Linux defines it. Every member
/// is fixed width and the assembly is asserted to be 256 bytes, so a
/// libc built for any port reads the same offsets.
struct [[gnu::packed]] StatxRecord {
    std::uint32_t mask;
    std::uint32_t blksize;
    std::uint64_t attributes;
    std::uint32_t nlink;
    std::uint32_t uid;
    std::uint32_t gid;
    std::uint16_t mode;
    std::uint16_t spare0;
    std::uint64_t ino;
    std::uint64_t size;
    std::uint64_t blocks;
    std::uint64_t attributes_mask;
    std::int64_t  atime_sec;
    std::uint32_t atime_nsec;
    std::int32_t  atime_reserved;
    std::int64_t  btime_sec;
    std::uint32_t btime_nsec;
    std::int32_t  btime_reserved;
    std::int64_t  ctime_sec;
    std::uint32_t ctime_nsec;
    std::int32_t  ctime_reserved;
    std::int64_t  mtime_sec;
    std::uint32_t mtime_nsec;
    std::int32_t  mtime_reserved;
    std::uint32_t rdev_major;
    std::uint32_t rdev_minor;
    std::uint32_t dev_major;
    std::uint32_t dev_minor;
    std::uint64_t mnt_id;
    std::uint32_t dio_mem_align;
    std::uint32_t dio_offset_align;
    std::uint64_t spare3[12];
};
static_assert(sizeof(StatxRecord) == 256, "statx records travel as 256 fixed bytes");
static_assert(offsetof(StatxRecord, ino) == 32, "statx ino sits at byte 32");
static_assert(offsetof(StatxRecord, mtime_sec) == 112, "statx mtime sits at byte 112");

/// one st_mode's byte count, the 512-byte unit blocks are counted in.
constexpr unsigned long kBlockUnit = 512;

}  // namespace

void WriteStatxRecord(unsigned char* sink, const fs::FsStat& probed) {
    auto* const record = reinterpret_cast<StatxRecord*>(sink);
    *record            = StatxRecord{};
    record->mask       = kStatxBasicStats;
    record->blksize    = 4096;
    record->nlink      = static_cast<std::uint32_t>(probed.links);
    record->mode       = static_cast<std::uint16_t>(probed.mode);
    record->ino        = probed.ino;
    record->size       = probed.size;
    record->blocks     = (probed.size + kBlockUnit - 1UL) / kBlockUnit;
    record->rdev_major = static_cast<std::uint32_t>(probed.rdev >> 8U);
    record->rdev_minor = static_cast<std::uint32_t>(probed.rdev & 0xFFU);
    record->mtime_sec  = static_cast<std::int64_t>(probed.mtime);
    record->atime_sec  = record->mtime_sec;
    record->ctime_sec  = record->mtime_sec;
}

long long HandleStatx(const SyscallFrame& frame) {
    const Word kDestination = frame.r8;
    if (!UserRangeWritable(kDestination, kStatxRecordBytes)) {
        return -kEfault;
    }
    // The one directory seat this kernel has is the current one; a
    // path arriving absolute ignores the seat as Linux does, so only a
    // relative path under a foreign seat is refused.
    // AT_EMPTY_PATH: an empty path names the descriptor's own node —
    // the shape a libc asks for when it folds fstat onto statx.
    constexpr unsigned long kAtEmptyPath = 0x1000;
    const bool kSeatIsCwd = (frame.rdi & 0xFFFFFFFFULL) == kAtFdCwd;
    if (!kSeatIsCwd && frame.rsi != 0) {
        char probe[fs::kFsPathMax] = {};
        if (FoldUserPath(frame.rsi, probe, sizeof(probe)) > 0 && probe[0] != '/') {
            return -kEbadf;
        }
    }
    // The raw string is read before any folding: an empty path is the
    // AT_EMPTY_PATH spelling of "the descriptor itself", and folding
    // rejects it as a path before we get to see it.
    // The first byte decides the shape: an empty string is the
    // AT_EMPTY_PATH spelling of "the descriptor itself", and the path
    // reader rejects empty input as a path before we get to see it.
    char first = 0;
    if (!uaccess::CopyFromUser(&first, frame.rsi, 1)) {
        return -kEfault;
    }
    // statx(dirfd, path, flags, mask, buf): the flags word is the
    // third argument, the mask the fourth. AT_EMPTY_PATH lives in the
    // flags, not in the mask.
    if (first == '\0' && (frame.rdx & kAtEmptyPath) != 0) {
        const base::Result<base::Shared<fs::OpenFile>> kSlot = LiveSlot(frame.rdi);
        if (!kSlot.ok()) {
            return -kEbadf;
        }
        if (kSlot.value()->ops->stat == nullptr) {
            return -kEnosys;
        }
        fs::FsStat probed{};
        if (!kSlot.value()->ops->stat(kSlot.value()->backend, kSlot.value()->node, probed).ok()) {
            return -kEio;
        }
        unsigned char record[kStatxRecordBytes] = {};
        WriteStatxRecord(record, probed);
        if (!uaccess::CopyToUser(kDestination, record, sizeof(record))) {
            return -kEfault;
        }
        return 0;
    }
    char path[fs::kFsPathMax] = {};
    if (const long long kBad = FoldUserPath(frame.rsi, path, sizeof(path)); kBad < 0) {
        return kBad;
    }
    const base::Result<fs::FsView> kView = fs::kernel_vfs().resolve(path);
    if (!kView.ok()) {
        return -ErrnoFrom(kView.error());
    }
    const fs::FsView& flat = kView.value();
    const base::Result<fs::FsNode> kNode = flat.ops->lookup(flat.self, flat.rest);
    if (!kNode.ok()) {
        return -ErrnoFrom(kNode.error());
    }
    fs::FsStat               probed{};
    // lookup hands back a borrowed node, not a held one: the release
    // belongs to whoever took a reference, and stat takes none.
    const base::Result<void> kProbed = flat.ops->stat(flat.self, kNode.value(), probed);
    if (!kProbed.ok()) {
        return -ErrnoFrom(kProbed.error());
    }

    unsigned char record[kStatxRecordBytes] = {};
    WriteStatxRecord(record, probed);
    if (!uaccess::CopyToUser(kDestination, record, sizeof(record))) {
        return -kEfault;
    }
    return 0;
}

}  // namespace cinux::syscall
