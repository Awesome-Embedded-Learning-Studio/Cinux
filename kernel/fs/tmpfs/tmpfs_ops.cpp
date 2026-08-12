/**
 * @file kernel/fs/tmpfs/tmpfs_ops.cpp
 * @brief TmpFs InodeOps implementations (TmpFileOps / TmpDirOps)
 *
 * Split from tmpfs.cpp to keep that file under the 500-line limit.  Both
 * InodeOps classes are pure logic (no kprintf / kernel-only I/O), so they
 * link into the kernel and the host unit tests alongside tmpfs.cpp, sharing
 * the tree-walk / stat helpers via tmpfs_internal.hpp.  The concrete classes
 * stay in this file's anonymous namespace; TmpFs reaches them only through
 * the make_tmpfs_file_ops / make_tmpfs_dir_ops factories.
 *
 * Namespace: cinux::fs
 */

#include "kernel/fs/tmpfs/tmpfs_internal.hpp"

#include <stddef.h>
#include <stdint.h>

#include "kernel/fs/file.hpp"     // inode_ref (create/mkdir transfer a ref)
#include "kernel/lib/string.hpp"  // memcpy / memset

namespace cinux::fs {

using cinux::lib::Error;
using cinux::lib::ErrorOr;

using tmpfs_detail::fill_dir_stat;
using tmpfs_detail::fill_dot_entry;
using tmpfs_detail::fill_reg_stat;
using tmpfs_detail::find_child;
using tmpfs_detail::name_matches;
using tmpfs_detail::release_unlinked_node;
using tmpfs_detail::round_up_capacity;

namespace {

class TmpFileOps : public InodeOps {
public:
    ErrorOr<int64_t> read(const Inode* inode, uint64_t offset, void* buf, uint64_t count) override {
        if (inode == nullptr || buf == nullptr) {
            return Error::InvalidArgument;
        }
        auto* node = static_cast<TmpNode*>(inode->fs_private);
        auto  g    = node->fs->lock_.guard();
        if (count == 0 || offset >= node->size) {
            return 0;  // nothing requested, or past EOF
        }
        uint64_t avail = node->size - offset;
        uint64_t n     = count < avail ? count : avail;
        memcpy(buf, node->data + offset, n);
        return static_cast<int64_t>(n);
    }

    ErrorOr<int64_t> write(Inode* inode, uint64_t offset, const void* buf,
                           uint64_t count) override {
        if (inode == nullptr || buf == nullptr) {
            return Error::InvalidArgument;
        }
        auto* node = static_cast<TmpNode*>(inode->fs_private);
        auto  g    = node->fs->lock_.guard();
        if (count == 0) {
            return 0;
        }
        uint64_t need = offset + count;
        if (need > node->capacity) {
            // Grow: allocate a fresh page-aligned buffer, copy the live prefix,
            // zero-fill any gap between the old EOF and the write offset, then
            // release the old buffer.
            uint64_t newcap = round_up_capacity(need);
            uint8_t* nd     = new uint8_t[newcap];
            if (node->size > 0) {
                memcpy(nd, node->data, node->size);
            }
            if (offset > node->size) {
                memset(nd + node->size, 0, offset - node->size);
            }
            delete[] node->data;
            node->data     = nd;
            node->capacity = newcap;
        } else if (offset > node->size) {
            // Gap inside the existing buffer: zero it so the file is not left
            // with stale heap bytes between the old EOF and the write.
            memset(node->data + node->size, 0, offset - node->size);
        }
        memcpy(node->data + offset, buf, count);
        if (need > node->size) {
            node->size = need;
        }
        node->inode.size = node->size;
        return static_cast<int64_t>(count);
    }

    ErrorOr<void> stat(const Inode* inode, struct stat* st) override {
        if (inode == nullptr || st == nullptr) {
            return Error::InvalidArgument;
        }
        auto* node = static_cast<TmpNode*>(inode->fs_private);
        auto  g    = node->fs->lock_.guard();
        fill_reg_stat(node, st);
        return {};
    }

    ErrorOr<void> truncate(Inode* inode, uint64_t length) override {
        if (inode == nullptr) {
            return Error::InvalidArgument;
        }
        auto* node = static_cast<TmpNode*>(inode->fs_private);
        auto  g    = node->fs->lock_.guard();
        (void)g;

        if (length > node->capacity) {
            uint64_t newcap = round_up_capacity(length);
            uint8_t* nd     = new uint8_t[newcap];
            if (node->size > 0) {
                memcpy(nd, node->data, node->size);
            }
            memset(nd + node->size, 0, length - node->size);
            delete[] node->data;
            node->data     = nd;
            node->capacity = newcap;
        } else if (length > node->size) {
            memset(node->data + node->size, 0, length - node->size);
        }

        node->size       = length;
        node->inode.size = length;
        return {};
    }

    ErrorOr<void> chmod(Inode* inode, uint32_t mode) override {
        if (inode == nullptr) {
            return Error::InvalidArgument;
        }
        auto* node = static_cast<TmpNode*>(inode->fs_private);
        auto  g    = node->fs->lock_.guard();
        (void)g;
        node->inode.mode = kTmpfsSIfReg | (mode & 07777);
        return {};
    }

    void release(Inode* inode) override { release_unlinked_node(inode); }
};

class TmpDirOps : public InodeOps {
public:
    ErrorOr<int64_t> readdir(const Inode* inode, uint64_t index, char* name,
                             uint64_t name_max) override {
        if (inode == nullptr || name == nullptr || name_max == 0) {
            return Error::InvalidArgument;
        }
        auto* dir = static_cast<TmpNode*>(inode->fs_private);
        auto  g   = dir->fs->lock_.guard();

        auto dot = fill_dot_entry(index, name, name_max);
        if (!dot.ok() || dot.value() == 1) {
            return dot;
        }

        // index >= 2 -> the (index-2)-th child in the sibling list.
        uint32_t skip = static_cast<uint32_t>(index - 2);
        TmpNode* c    = dir->first_child;
        while (skip > 0 && c != nullptr) {
            c = c->next_sibling;
            --skip;
        }
        if (c == nullptr) {
            return 0;  // past the last entry
        }
        uint32_t i = 0;
        while (i + 1 < name_max && c->name[i] != '\0') {
            name[i] = c->name[i];
            ++i;
        }
        name[i] = '\0';
        return 1;
    }

    ErrorOr<Inode*> create(Inode* dir_inode, const char* name, uint32_t namelen) override {
        return make_node(dir_inode, name, namelen, InodeType::Regular);
    }

    ErrorOr<Inode*> mkdir(Inode* dir_inode, const char* name, uint32_t namelen) override {
        return make_node(dir_inode, name, namelen, InodeType::Directory);
    }

    ErrorOr<void> unlink(Inode* dir_inode, const char* name, uint32_t namelen) override {
        if (dir_inode == nullptr || name == nullptr || namelen == 0) {
            return Error::InvalidArgument;
        }
        auto*    dir      = static_cast<TmpNode*>(dir_inode->fs_private);
        TmpNode* detached = nullptr;
        {
            auto g = dir->fs->lock_.guard();

            // Walk the child list keeping a prev pointer so we can splice the
            // matching entry out.
            TmpNode* prev = nullptr;
            TmpNode* c    = dir->first_child;
            while (c != nullptr && !name_matches(c, name, namelen)) {
                prev = c;
                c    = c->next_sibling;
            }
            if (c == nullptr) {
                return Error::NotFound;
            }
            // Removing a non-empty directory is rejected.  The Error enum has no
            // DirectoryNotEmpty, so this maps to EIO at the syscall boundary; the
            // contract (the op simply fails) is what callers and tests rely on.
            if (c->inode.type == InodeType::Directory && c->first_child != nullptr) {
                return Error::IOError;
            }
            if (prev == nullptr) {
                dir->first_child = c->next_sibling;
            } else {
                prev->next_sibling = c->next_sibling;
            }
            c->parent       = nullptr;
            c->next_sibling = nullptr;
            c->inode.nlink  = 0;
            detached        = c;
        }

        // Drop the directory entry's namespace reference outside lock_: the
        // zero transition invokes release(), which reacquires lock_ and is the
        // only path that may delete the detached node.
        inode_unref(&detached->inode);
        return {};
    }

    ErrorOr<void> stat(const Inode* inode, struct stat* st) override {
        if (inode == nullptr || st == nullptr) {
            return Error::InvalidArgument;
        }
        auto* node = static_cast<TmpNode*>(inode->fs_private);
        auto  g    = node->fs->lock_.guard();
        fill_dir_stat(node, st);
        return {};
    }

    ErrorOr<void> chmod(Inode* inode, uint32_t mode) override {
        if (inode == nullptr) {
            return Error::InvalidArgument;
        }
        auto* node = static_cast<TmpNode*>(inode->fs_private);
        auto  g    = node->fs->lock_.guard();
        (void)g;
        node->inode.mode = kTmpfsSIfDir | (mode & 07777);
        return {};
    }

    void release(Inode* inode) override { release_unlinked_node(inode); }

private:
    /// Shared body of create() / mkdir(): validate the name, reject a duplicate,
    /// allocate + link a new TmpNode of @p type, and return its Inode.
    ErrorOr<Inode*> make_node(Inode* dir_inode, const char* name, uint32_t namelen,
                              InodeType type) {
        if (dir_inode == nullptr || name == nullptr || namelen == 0) {
            return Error::InvalidArgument;
        }
        if (namelen + 1 > kTmpfsNameMax) {
            return Error::InvalidArgument;  // name too long
        }
        auto* dir = static_cast<TmpNode*>(dir_inode->fs_private);
        auto  g   = dir->fs->lock_.guard();

        if (find_child(dir, name, namelen) != nullptr) {
            return Error::AlreadyExists;
        }

        auto* node = new TmpNode{};  // value-initialised: pointers null, sizes 0
        memcpy(node->name, name, namelen);
        node->name[namelen] = '\0';
        node->fs            = dir->fs;
        node->parent        = dir;
        node->inode.ino     = dir->fs->alloc_ino();
        node->inode.type    = type;
        node->inode.ops = (type == InodeType::Directory) ? dir->fs->dir_ops() : dir->fs->file_ops();
        node->inode.fs_private = node;
        node->inode.mode =
            (type == InodeType::Directory) ? (kTmpfsSIfDir | 0755) : (kTmpfsSIfReg | 0644);
        node->inode.nlink = (type == InodeType::Directory) ? 2 : 1;

        // Prepend to the parent's child list.
        node->next_sibling = dir->first_child;
        dir->first_child   = node;
        inode_ref(&node->inode);  // directory entry owns one namespace reference
        inode_ref(&node->inode);  // create/mkdir transfers one reference to the caller
        return &node->inode;
    }
};

}  // namespace

InodeOps* make_tmpfs_file_ops() {
    return new TmpFileOps();
}

InodeOps* make_tmpfs_dir_ops() {
    return new TmpDirOps();
}

}  // namespace cinux::fs
