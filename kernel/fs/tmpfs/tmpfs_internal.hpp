/**
 * @file kernel/fs/tmpfs/tmpfs_internal.hpp
 * @brief TmpFs implementation internals shared across the tmpfs*.cpp units
 *
 * Split from tmpfs.cpp to keep that file under the 500-line limit.  INTERNAL
 * to the tmpfs implementation -- do not include outside kernel/fs/tmpfs/.
 *
 * Exposes:
 *   - TmpNode: the heap tree node backing one entry.  Its full definition
 *     lives here; tmpfs.hpp only forward-declares it so the public TmpFs
 *     interface stays opaque (holds it as a pointer).
 *   - tmpfs_detail::*: tree-walk / stat / release helpers used by BOTH the
 *     TmpFileOps / TmpDirOps InodeOps (tmpfs_ops.cpp) and the TmpFs backend
 *     (tmpfs.cpp).  They were lifted out of tmpfs.cpp's anonymous namespace
 *     so both translation units can call them.
 *   - make_tmpfs_file_ops / make_tmpfs_dir_ops: factories returning the
 *     shared InodeOps instances (the concrete classes stay hidden in
 *     tmpfs_ops.cpp).
 *
 * Namespace: cinux::fs (TmpNode) / cinux::fs::tmpfs_detail (helpers)
 */

#pragma once

#include <stdint.h>

#include <cinux/expected.hpp>  // ErrorOr

#include "fs/vfs_filesystem.hpp"   // Inode, InodeOps, struct stat (via inode.hpp)
#include "kernel/fs/tmpfs/tmpfs.hpp"  // TmpFs, kTmpfsNameMax, kTmpfs* constants

namespace cinux::fs {

/// Heap tree node backing one tmpfs entry.  Full definition lives here;
/// tmpfs.hpp forward-declares it so TmpFs holds it opaquely.  Regular files
/// store their content in the growable @p data buffer; directories chain
/// children through @p first_child / @p next_sibling.
struct TmpNode {
    Inode    inode;                ///< Embedded; ops + fs_private set by TmpFs
    TmpFs*   fs;                   ///< Owning filesystem (lock + ino allocator)
    TmpNode* parent;               ///< Parent directory (nullptr for the root)
    TmpNode* next_sibling;         ///< Next entry in parent->first_child chain
    TmpNode* first_child;          ///< (dirs) head of the child list
    char     name[kTmpfsNameMax];  ///< NUL-terminated entry name
    uint8_t* data;                 ///< (files) heap byte buffer
    uint64_t capacity;             ///< (files) allocated bytes in data
    uint64_t size;                 ///< (files) logical content length in bytes
};

namespace tmpfs_detail {

/// Last-inode-reference hook shared by regular files and directories.
///
/// Every linked node owns a namespace inode reference, so a linked node can
/// never reach refcount zero.  unlink drops that reference only after removing
/// the entry; therefore this last-ref callback is the sole deletion owner and
/// cannot race another path that observed refcount zero.
void release_unlinked_node(Inode* inode);

/// Round @p need up to a whole number of kTmpfsGrowthAlign units (at least one
/// unit), so a stream of small writes into one file reallocates O(log) times.
uint64_t round_up_capacity(uint64_t need);

/// Does @p entry's NUL-terminated name equal @p name[0 .. namelen)?
bool name_matches(const TmpNode* entry, const char* name, uint32_t namelen);

/// Walk @p dir's child list for an entry matching @p name[0 .. namelen).
/// Returns nullptr when no child matches.
TmpNode* find_child(const TmpNode* dir, const char* name, uint32_t namelen);

/// Fill a struct stat for a regular-file TmpNode.  Zeroed first so no
/// kernel-stack bytes leak through the Linux-ABI padding / _nsec fields.
void fill_reg_stat(const TmpNode* node, struct stat* st);

/// Fill a struct stat for a directory TmpNode.
void fill_dir_stat(const TmpNode* node, struct stat* st);

/// Emit the "." (index 0) or ".." (index 1) readdir entry into @p name.
/// Returns 1 on a match, InvalidArgument when @p name_max is too small, or
/// leaves index >= 2 to the caller by returning 0.
cinux::lib::ErrorOr<int64_t> fill_dot_entry(uint64_t index, char* name, uint64_t name_max);

/// Recursively free @p node and its descendants (depth-first).  Used by the
/// TmpFs destructor so a stack-local TmpFs (unit tests) leaks nothing.
void free_tree(TmpNode* node);

}  // namespace tmpfs_detail

/// Construct the shared regular-file InodeOps.  Owned by the caller (TmpFs).
/// The concrete class is hidden in tmpfs_ops.cpp.
InodeOps* make_tmpfs_file_ops();

/// Construct the shared directory InodeOps.  Owned by the caller (TmpFs).
/// The concrete class is hidden in tmpfs_ops.cpp.
InodeOps* make_tmpfs_dir_ops();

}  // namespace cinux::fs
