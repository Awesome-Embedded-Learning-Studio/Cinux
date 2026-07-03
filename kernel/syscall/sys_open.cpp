/**
 * @file kernel/syscall/sys_open.cpp
 * @brief sys_open handler implementation
 *
 * Resolves a path through the VFS, looks up the Inode, and allocates
 * a file descriptor in the global FDTable.
 */

#include "kernel/syscall/sys_open.hpp"

#include <stdint.h>

#include "kernel/errno.hpp"
#include "kernel/fs/file.hpp"
#include "kernel/fs/path.hpp"
#include "kernel/fs/vfs_mount.hpp"
#include "kernel/lib/kprintf.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/syscall/path_util.hpp"

namespace cinux::syscall {

int64_t do_open_kernel(const char* resolved_path, uint64_t flags) {
    // Step 1: Resolve through the VFS mount table
    const char*            rel_path = nullptr;
    cinux::fs::FileSystem* fs       = cinux::fs::vfs_resolve(resolved_path, &rel_path);

    if (fs == nullptr) {
        cinux::lib::kprintf("[SYS_OPEN] No filesystem mounted for '%s'\n", resolved_path);
        return -kEnoent;
    }

    // Step 2: Look up the Inode in the backend filesystem
    auto inode_result = fs->lookup(rel_path);
    if (!inode_result.ok()) {
        cinux::lib::kprintf("[SYS_OPEN] File not found: '%s'\n", resolved_path);
        return -to_errno(inode_result.error());
    }
    cinux::fs::Inode* inode = inode_result.value();

    // Let the inode's open() substitute a per-open inode: a cloning device such
    // as /dev/ptmx allocates a fresh PTY pair here and returns the master inode
    // for the new fd; a FIFO returns the read or write end of its pipe (honouring
    // the open direction in @p flags).  The default returns the same inode.
    if (inode->ops != nullptr) {
        auto opened = inode->ops->open(inode, flags);
        if (!opened.ok()) {
            return -to_errno(opened.error());
        }
        inode = opened.value();
    }

    // Step 3: Convert flags to OpenFlags
    cinux::fs::OpenFlags open_flags;
    switch (flags) {
    case 0:
        open_flags = cinux::fs::OpenFlags::RDONLY;
        break;
    case 1:
        open_flags = cinux::fs::OpenFlags::WRONLY;
        break;
    case 2:
        open_flags = cinux::fs::OpenFlags::RDWR;
        break;
    default:
        open_flags = cinux::fs::OpenFlags::RDONLY;
        break;
    }

    // Step 4: Allocate a file descriptor
    int fd = cinux::fs::current_fd_table().alloc(inode, open_flags);

    if (fd == cinux::fs::FD_NONE) {
        cinux::lib::kprintf("[SYS_OPEN] FD table full, cannot open '%s'\n", resolved_path);
        return -kEmfile;
    }

    return static_cast<int64_t>(fd);
}

int64_t sys_open(uint64_t path_virt, uint64_t flags, uint64_t mode, uint64_t, uint64_t, uint64_t) {
    // Boundary: resolve the user path (cwd-aware), then run kernel logic.
    cinux::fs::PathBuf resolved;
    if (!resolve_user_path(path_virt, resolved.data())) {
        return -kEfault;
    }
    return do_openat_kernel(resolved.data(), flags, mode);
}

// ============================================================
// sys_openat (F10-M1 batch 4) -- musl open()/fopen() entry point
// ============================================================

namespace {

/// Linux open() flag bits (x86-64 UAPI).
constexpr uint64_t kOAccessMode = 0x3;      ///< mask: 0=RDONLY,1=WRONLY,2=RDWR
constexpr uint64_t kOCreat      = 0x40;     ///< create if missing
constexpr uint64_t kOTrunc      = 0x200;    ///< truncate to 0 on open (O_TRUNC)
[[maybe_unused]] constexpr uint64_t kOCloexec    = 0x80000;  ///< close-on-exec (recorded by FDTable later)

/// AT_FDCWD: "relative to current working directory".
[[maybe_unused]] constexpr int64_t kAtFdcwd = -100;

/// Map Linux access-mode bits to CinuxOS OpenFlags.
cinux::fs::OpenFlags access_to_open_flags(uint64_t flags) {
    switch (flags & kOAccessMode) {
    case 1:
        return cinux::fs::OpenFlags::WRONLY;
    case 2:
        return cinux::fs::OpenFlags::RDWR;
    default:
        return cinux::fs::OpenFlags::RDONLY;
    }
}

uint32_t create_mode_from(uint64_t mode) {
    uint32_t mask = 0;
    if (auto* task = cinux::proc::Scheduler::current()) {
        mask = task->umask & 0777;
    }
    return static_cast<uint32_t>(mode) & 0777 & ~mask;
}

}  // anonymous namespace

int64_t do_openat_kernel(const char* resolved_path, uint64_t flags, uint64_t mode) {
    const char*            rel_path = nullptr;
    cinux::fs::FileSystem* fs       = cinux::fs::vfs_resolve(resolved_path, &rel_path);
    if (fs == nullptr) {
        cinux::lib::kprintf("[SYS_OPENAT] No filesystem mounted for '%s'\n", resolved_path);
        return -kEnoent;
    }

    bool created      = false;
    auto inode_result = fs->lookup(rel_path);
    if (!inode_result.ok()) {
        // Missing file: create it if O_CREAT, otherwise it is an error.
        if (!(flags & kOCreat)) {
            return -to_errno(inode_result.error());
        }
        cinux::fs::PathBuf parent_buf;
        const char*        leaf_name = nullptr;
        uint32_t           name_len  = 0;
        if (!split_pathname(rel_path, parent_buf, &leaf_name, &name_len)) {
            return -kEinval;
        }
        auto parent_result = fs->lookup(parent_buf);
        if (!parent_result.ok() || parent_result.value()->ops == nullptr) {
            return parent_result.ok() ? -kEio : -to_errno(parent_result.error());
        }
        auto create_result =
            parent_result.value()->ops->create(parent_result.value(), leaf_name, name_len);
        if (!create_result.ok()) {
            return -to_errno(create_result.error());
        }
        created      = true;
        inode_result = fs->lookup(rel_path);
        if (!inode_result.ok()) {
            return -to_errno(inode_result.error());
        }
    }
    cinux::fs::Inode* inode = inode_result.value();

    if (created && inode->ops != nullptr) {
        auto chmod_result = inode->ops->chmod(inode, create_mode_from(mode));
        if (!chmod_result.ok() && chmod_result.error() != cinux::lib::Error::NotImplemented) {
            return -to_errno(chmod_result.error());
        }
    }

    // O_TRUNC: truncate the file to 0 before handing out the fd.  Without this
    // a shorter rewrite leaves the old tail (B4-C2: cc1 overwrote the host-
    // precompiled /hello.s but the residual "...progbits\n" tail -> as saw
    // "gbits" -> "no such instruction").
    if ((flags & kOTrunc) != 0 && inode->ops != nullptr) {
        auto tr = inode->ops->truncate(inode, 0);
        if (!tr.ok()) {
            return -to_errno(tr.error());
        }
    }

    int fd = cinux::fs::current_fd_table().alloc(inode, access_to_open_flags(flags));
    if (fd == cinux::fs::FD_NONE) {
        return -kEmfile;
    }
    // close-on-exec not yet wired into FDTable
    return static_cast<int64_t>(fd);
}

// ============================================================
// sys_openat (F10-M1 batch 4) -- musl open()/fopen() entry point
// ============================================================

int64_t sys_openat([[maybe_unused]] uint64_t dirfd, uint64_t path_virt, uint64_t flags, uint64_t mode, uint64_t,
                   uint64_t) {
    // Only AT_FDCWD (-100) is meaningful today; a real dirfd would need per-fd
    // path tracking.  musl always passes AT_FDCWD, so we resolve cwd-relative
    // regardless.  (Documented limitation until per-fd paths are tracked.)

    // Boundary: resolve the user path (cwd-aware), then run kernel logic.
    cinux::fs::PathBuf resolved;
    if (!resolve_user_path(path_virt, resolved.data())) {
        return -kEfault;
    }
    return do_openat_kernel(resolved.data(), flags, mode);
}

}  // namespace cinux::syscall
