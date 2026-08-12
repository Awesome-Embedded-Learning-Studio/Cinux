/**
 * @file kernel/fs/tmpfs/tmpfs.cpp
 * @brief TmpFs backend: tree management + shared tmpfs_detail helpers
 *
 * The InodeOps implementations (TmpFileOps / TmpDirOps) live in tmpfs_ops.cpp;
 * TmpNode and the tmpfs_detail helpers they share are declared in
 * tmpfs_internal.hpp.  This file holds the helper definitions plus the TmpFs
 * FileSystem backend (mount / lookup / lookup_child).  Like tmpfs_ops.cpp it
 * is pure logic (no kprintf / kernel-only I/O) so it links into both the
 * kernel and the host unit tests.
 *
 * Namespace: cinux::fs
 */

#include "kernel/fs/tmpfs/tmpfs_internal.hpp"

#include <stddef.h>
#include <stdint.h>

#include "kernel/fs/file.hpp"     // inode_ref (lookup returns a ref'd Inode*)
#include "kernel/lib/string.hpp"  // memset (stat helpers)

namespace cinux::fs {

using cinux::lib::Error;
using cinux::lib::ErrorOr;

// ============================================================
// Shared helpers (tmpfs_detail) -- definitions
//
// Lives in a named (non-anonymous) namespace so tmpfs_ops.cpp can call them
// too.  See tmpfs_internal.hpp for the contracts.
// ============================================================

namespace tmpfs_detail {

void release_unlinked_node(Inode* inode) {
    if (inode == nullptr || inode->fs_private == nullptr) {
        return;
    }
    auto* node = static_cast<TmpNode*>(inode->fs_private);
    auto  g    = node->fs->lock_.guard();
    if (node->inode.nlink == 0 && __atomic_load_n(&node->inode.refcount, __ATOMIC_ACQUIRE) == 0) {
        delete[] node->data;  // delete[] nullptr is a safe no-op
        delete node;
    }
}

uint64_t round_up_capacity(uint64_t need) {
    constexpr uint64_t kAlign = kTmpfsGrowthAlign;
    if (need == 0) {
        return kAlign;
    }
    return ((need + kAlign - 1) / kAlign) * kAlign;
}

bool name_matches(const TmpNode* entry, const char* name, uint32_t namelen) {
    for (uint32_t i = 0; i < namelen; ++i) {
        if (entry->name[i] == '\0' || entry->name[i] != name[i]) {
            return false;
        }
    }
    return entry->name[namelen] == '\0';  // entry is not longer than namelen
}

TmpNode* find_child(const TmpNode* dir, const char* name, uint32_t namelen) {
    for (TmpNode* c = dir->first_child; c != nullptr; c = c->next_sibling) {
        if (name_matches(c, name, namelen)) {
            return c;
        }
    }
    return nullptr;
}

void fill_reg_stat(const TmpNode* node, struct stat* st) {
    memset(st, 0, sizeof(*st));
    st->st_ino     = node->inode.ino;
    st->st_nlink   = node->inode.nlink;
    st->st_mode    = node->inode.mode;
    st->st_size    = node->size;
    st->st_blksize = 4096;
    st->st_blocks  = (node->size + 511) / 512;
}

void fill_dir_stat(const TmpNode* node, struct stat* st) {
    memset(st, 0, sizeof(*st));
    st->st_ino     = node->inode.ino;
    st->st_nlink   = node->inode.nlink;
    st->st_mode    = node->inode.mode;
    st->st_blksize = 4096;
}

ErrorOr<int64_t> fill_dot_entry(uint64_t index, char* name, uint64_t name_max) {
    if (index == 0) {
        if (name_max < 2) {
            return Error::InvalidArgument;
        }
        name[0] = '.';
        name[1] = '\0';
        return 1;
    }
    if (index == 1) {
        if (name_max < 3) {
            return Error::InvalidArgument;
        }
        name[0] = '.';
        name[1] = '.';
        name[2] = '\0';
        return 1;
    }
    return 0;
}

void free_tree(TmpNode* node) {
    if (node == nullptr) {
        return;
    }
    TmpNode* c = node->first_child;
    while (c != nullptr) {
        TmpNode* next = c->next_sibling;
        free_tree(c);
        c = next;
    }
    delete[] node->data;  // delete[] nullptr is safe
    delete node;
}

}  // namespace tmpfs_detail

// ============================================================
// TmpFs backend
// ============================================================

TmpFs::TmpFs() = default;

TmpFs::~TmpFs() {
    tmpfs_detail::free_tree(root_);
    delete file_ops_;
    delete dir_ops_;
}

ErrorOr<void> TmpFs::mount() {
    // Idempotent: a second mount() is a no-op (mirrors DevFs / ProcFs).
    if (mounted_) {
        return {};
    }

    file_ops_ = make_tmpfs_file_ops();
    dir_ops_  = make_tmpfs_dir_ops();

    root_               = new TmpNode{};
    root_->fs           = this;
    root_->parent       = nullptr;
    root_->next_sibling = nullptr;
    root_->first_child  = nullptr;
    root_->data         = nullptr;
    root_->capacity     = 0;
    root_->size         = 0;
    root_->name[0]      = '\0';

    root_->inode.ino        = 1;
    root_->inode.type       = InodeType::Directory;
    root_->inode.ops        = dir_ops_;
    root_->inode.fs_private = root_;
    root_->inode.mode       = kTmpfsSIfDir | 0755;
    root_->inode.nlink      = 2;
    inode_ref(&root_->inode);  // the mounted root owns one namespace reference

    mounted_ = true;
    return {};
}

ErrorOr<Inode*> TmpFs::lookup(const char* path) {
    if (path == nullptr) {
        return Error::InvalidArgument;
    }
    auto g = lock_.guard();

    if (root_ == nullptr) {
        return Error::IOError;  // not mounted
    }

    // Root directory: empty path or "/".
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        inode_ref(&root_->inode);
        return &root_->inode;
    }
    const char* p = path;
    if (p[0] == '/') {
        ++p;
    }

    // Walk component by component.  Each intermediate component must resolve to
    // a directory; a missing component or a non-directory intermediate yields
    // NotFound (mirrors ext2::lookup's contract).
    TmpNode* cur = root_;
    while (p[0] != '\0') {
        uint32_t comp_len = 0;
        while (p[comp_len] != '\0' && p[comp_len] != '/') {
            ++comp_len;
        }
        if (comp_len == 0) {
            ++p;  // collapse a duplicate slash
            continue;
        }
        if (cur->inode.type != InodeType::Directory) {
            return Error::NotFound;  // cannot descend through a file
        }
        TmpNode* child = tmpfs_detail::find_child(cur, p, comp_len);
        if (child == nullptr) {
            return Error::NotFound;
        }
        cur = child;
        p += comp_len;
        if (p[0] == '/') {
            ++p;
        }
    }
    inode_ref(&cur->inode);
    return &cur->inode;
}

// F-USABILITY batch 1a: single-component lookup for the vfs_lookup layer.
// Recovers the parent TmpNode via fs_private, then delegates to find_child
// (the same primitive lookup() walks with) under the fs lock.
ErrorOr<Inode*> TmpFs::lookup_child(const Inode* parent, const char* name, uint32_t namelen) {
    if (parent == nullptr || name == nullptr || namelen == 0) {
        return Error::InvalidArgument;
    }
    if (parent->type != InodeType::Directory || parent->fs_private == nullptr) {
        return Error::NotFound;
    }
    auto g = lock_.guard();
    (void)g;
    TmpNode* parent_node = static_cast<TmpNode*>(parent->fs_private);
    TmpNode* child       = tmpfs_detail::find_child(parent_node, name, namelen);
    if (child == nullptr) {
        return Error::NotFound;
    }
    inode_ref(&child->inode);
    return &child->inode;
}

}  // namespace cinux::fs
