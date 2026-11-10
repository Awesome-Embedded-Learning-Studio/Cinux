/**
 * @file    fs_context.hpp
 * @brief   The file-system context a task carries: working directory,
 *          root and umask, shareable through clone.
 *
 * Everything that describes where a task stands in the file-system
 * tree lives in one counted object. fork copies the object so the
 * child walks away with its own working directory; CLONE_FS shares it
 * instead, so a chdir in one thread moves the whole group. Only the
 * working directory is consulted by syscalls today; the root and the
 * umask are carried here so chroot and umask have one object to
 * write, and no code path reads either yet.
 *
 * @author  Charliechen114514
 * @date    2026-11-08
 * @version 0.1
 * @since   0.9.0
 * @ingroup kernel_fs
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/memory.hpp"
#include "cinux/ref_count.hpp"
#include "cinux/shared.hpp"
#include "kernel/fs/fs_config.hpp"

namespace cinux::fs {

/**
 * @brief         One task's standing in the file-system tree.
 * @note          Counted because a CLONE_FS thread group holds the
 *                same object; the last member out returns the storage.
 *                Paths are absolute and NUL-terminated, the same
 *                spelling the old per-task array carried.
 * @since         0.9.0
 * @ingroup       kernel_fs
 */
struct FsContext : cinux::base::RefCount<> {
    /// Working directory, absolute, NUL-terminated.
    char         cwd[kFsPathMax]  = {'/', '\0'};
    /// Root the path walk stops at, absolute, NUL-terminated. No
    /// syscall writes it yet; chroot will.
    char         root[kFsPathMax] = {'/', '\0'};
    /// Creation-mask default; no syscall consults it yet.
    unsigned int umask            = 0022U;
};

/**
 * @brief         Make an independent copy of a context for a forked child.
 * @param[in]     source   The parent's context.
 * @return        A fresh counted context with the same paths and mask.
 * @note          A failed allocation returns an empty Shared; the
 *                fork path treats that as a failed spawn.
 * @since         0.9.0
 * @ingroup       kernel_fs
 */
[[nodiscard]] inline cinux::base::Shared<FsContext> CloneFsContext(const FsContext& source) {
    cinux::base::Shared<FsContext> copy = cinux::base::MakeShared<FsContext>();
    if (!copy.holds()) {
        return copy;
    }
    cinux::base::CopyBytes(copy->cwd, source.cwd, kFsPathMax);
    cinux::base::CopyBytes(copy->root, source.root, kFsPathMax);
    copy->umask = source.umask;
    return copy;
}

}  // namespace cinux::fs
