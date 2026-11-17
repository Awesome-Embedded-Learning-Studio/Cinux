/**
 * @file    tmpfs_mount.cpp
 * @brief   Mounting tmpfs: the volume seats behind the mount syscall.
 *
 * One seat table for the tmpfs volumes a process may ask for: each
 * seat owns a RamFs and its lease, and the mount table itself keeps
 * the prefix. Both ports reach this through the same syscall face;
 * the x86 singleton no longer owns the arrangement.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_fs
 * @copyright Copyright (c) 2026
 */

#include "kernel/fs/tmpfs_mount.hpp"

#include "kernel/proc/sync.hpp"
#include "kernel/fs/kernel_vfs.hpp"
#include "kernel/fs/mount_lease.hpp"
#include "kernel/fs/ramfs/ramfs.hpp"
#include "kernel/fs/vfs.hpp"

namespace cinux::fs {

namespace {

constexpr unsigned long kTmpFsSeatMax = 4;

/// One mounted tmpfs: volume, lease, and the prefix it answers to.
struct TmpFsSeat {
    RamFs*      volume      = nullptr;
    MountLease* lifetime    = nullptr;
    char        prefix[kFsPathMax]{};
    unsigned long prefix_len = 0;
};

TmpFsSeat  g_seats[kTmpFsSeatMax] = {};
proc::Mutex g_gate{};  ///< Serializes runtime mount-seat updates.

}  // namespace

base::Result<void> mount_tmpfs(std::string_view target) {
    const arch::IrqGuard kIrq;
    const auto           kGuard = g_gate.guard();
    const auto           kView  = kernel_vfs().resolve(target);
    if (!kView.ok()) {
        return kView.error();
    }
    const auto kNode = kView.value().ops->lookup(kView.value().self, kView.value().rest);
    if (!kNode.ok()) {
        return kNode.error();
    }
    if (kNode.value().type != InodeType::kDirectory) {
        return base::KernelError::kNotADirectory;
    }
    for (TmpFsSeat& seat : g_seats) {
        if (seat.volume != nullptr) {
            continue;
        }
        seat.volume = new RamFs();
        if (seat.volume == nullptr) {
            return base::KernelError::kOutOfMemory;
        }
        seat.lifetime = new MountLease{};
        if (seat.lifetime == nullptr) {
            delete seat.volume;
            seat.volume = nullptr;
            return base::KernelError::kOutOfMemory;
        }
        seat.lifetime->backend = seat.volume;
        seat.lifetime->destroy = [](void* backend) { delete static_cast<RamFs*>(backend); };
        const base::Result<void> kSeated =
            kernel_vfs().mount(target, *seat.volume, "tmpfs", seat.lifetime);
        if (!kSeated.ok()) {
            if (seat.lifetime->release()) {
                delete seat.lifetime;
            }
            seat.lifetime = nullptr;
            seat.volume   = nullptr;
            return kSeated.error();
        }
        seat.prefix_len = target.size();
        base::CopyBytes(seat.prefix, target.data(), target.size());
        return {};
    }
    return base::KernelError::kOutOfMemory;
}

base::Result<void> umount_tmpfs(std::string_view target, bool lazy) {
    const arch::IrqGuard kIrq;
    const auto           kGuard = g_gate.guard();
    for (TmpFsSeat& seat : g_seats) {
        if (seat.volume == nullptr || std::string_view(seat.prefix, seat.prefix_len) != target) {
            continue;
        }
        if (!lazy && seat.lifetime->use_count() > 1) {
            return base::KernelError::kWouldBlock;
        }
        const base::Result<void> kGone = kernel_vfs().unmount(target);
        if (!kGone.ok()) {
            return kGone.error();
        }
        if (seat.lifetime->release()) {
            delete seat.lifetime;
        }
        seat.lifetime    = nullptr;
        seat.volume      = nullptr;
        seat.prefix_len  = 0;
        return {};
    }
    return base::KernelError::kNotFound;
}

}  // namespace cinux::fs
