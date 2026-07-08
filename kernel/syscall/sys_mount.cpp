/**
 * @file kernel/syscall/sys_mount.cpp
 * @brief sys_mount handler implementation (F6-M1)
 *
 * fstype-driven filesystem factory.  tmpfs is heap-allocated per mount and
 * registered as owned (sys_umount2 reclaims it).  proc/devfs share the boot
 * singletons (owned=false: a second mount point never frees the singleton).
 * ext2/ext4 (source -> IBlockDevice -> new Ext2) and other fstypes land in B1b;
 * until then they yield -ENODEV.  The @p source and @p flags (MS_*) arguments
 * are accepted for Linux ABI parity but unused by the fstypes wired so far.  The
 * boot /tmp mount uses the static g_tmpfs via tmpfs::init() (tmpfs_init.cpp).
 */

#include "kernel/syscall/sys_mount.hpp"

#include <stdint.h>

#include <memory>  // std::unique_ptr (RAII over raw new/delete; EXEMPT-reviewed)

#include "kernel/errno.hpp"
#include "kernel/fs/devfs/devfs.hpp"    // devfs::instance() (sys_mount -t devfs)
#include "kernel/fs/path.hpp"           // PathBuf
#include "kernel/fs/procfs/procfs.hpp"  // procfs::instance() (sys_mount -t proc)
#include "kernel/fs/tmpfs/tmpfs.hpp"    // TmpFs
#include "kernel/fs/vfs_mount.hpp"      // vfs_mount_add
#include "kernel/lib/kprintf.hpp"
#include "kernel/lib/string.hpp"         // strcmp
#include "kernel/syscall/path_util.hpp"  // resolve_user_path / read_user_path

namespace cinux::syscall {

using cinux::fs::FileSystem;
using cinux::fs::TmpFs;
using cinux::fs::vfs_mount_add;
using cinux::lib::kprintf;

int64_t do_mount_kernel([[maybe_unused]] const char* source, const char* target, const char* fstype,
                        [[maybe_unused]] uint64_t flags) {
    // @p source is unused until ext2/ext4 (B1b resolves it to an IBlockDevice);
    // tmpfs/proc/devfs have no backing device.  MS_* flags are accepted for Linux
    // ABI parity but not yet modelled.

    if (target == nullptr || fstype == nullptr || target[0] == '\0') {
        return -kEinval;
    }

    // ---- tmpfs: heap-allocated per mount; owned (sys_umount2 frees the tree) ----
    if (strcmp(fstype, "tmpfs") == 0) {
        // unique_ptr owns TmpFs until mount() succeeds; auto-freed on any error leg.
        std::unique_ptr<TmpFs> tfs(new TmpFs());
        auto                   m = tfs->mount();
        if (!m.ok()) {
            kprintf("[SYS_MOUNT] tmpfs mount() failed: %s\n", cinux::lib::error_string(m.error()));
            return -to_errno(m.error());
        }
        FileSystem* fs = tfs.release();
        if (!vfs_mount_add(target, fs, /*owned=*/true)) {
            kprintf("[SYS_MOUNT] mount table full, cannot mount '%s'\n", target);
            delete fs;  // mount table did not take ownership; reclaim
            return -kEnomem;
        }
        return 0;
    }

    // ---- proc / devfs: boot singletons.  Mounting at a second path shares the
    // one instance; owned=false means sys_umount2 detaches the mount point but
    // never frees the singleton (it outlives any one mount point, as at /proc).
    if (strcmp(fstype, "proc") == 0 || strcmp(fstype, "devfs") == 0) {
        FileSystem* fs = (strcmp(fstype, "proc") == 0)
                             ? static_cast<FileSystem*>(cinux::fs::procfs::instance())
                             : static_cast<FileSystem*>(cinux::fs::devfs::instance());
        if (fs == nullptr) {
            return -kEnodev;  // the FS was never initialised at boot
        }
        if (!vfs_mount_add(target, fs, /*owned=*/false)) {
            kprintf("[SYS_MOUNT] mount table full, cannot mount '%s'\n", target);
            return -kEnomem;
        }
        return 0;
    }

    // ---- ext2 / ext4 (B1b: source -> IBlockDevice -> new Ext2) / ramfs (no such
    // FS) / anything else: not supported yet.  Linux returns ENODEV for an
    // unknown filesystem type.
    kprintf("[SYS_MOUNT] unknown filesystem type '%s'\n", fstype);
    return -kEnodev;
}

int64_t sys_mount([[maybe_unused]] uint64_t source_virt, uint64_t target_virt, uint64_t fstype_virt, uint64_t flags,
                  [[maybe_unused]] uint64_t data, uint64_t) {
    // mount options string -- not yet parsed

    // source is unused until ext2/ext4 (B1b); do not read it (tolerates a NULL
    // source, which Linux permits for source-less filesystems like tmpfs).

    cinux::fs::PathBuf target;
    if (!resolve_user_path(target_virt, target.data())) {
        return -kEfault;
    }

    char fstype[32];
    if (!read_user_path(fstype_virt, fstype, sizeof(fstype))) {
        return -kEfault;
    }

    return do_mount_kernel(/*source=*/nullptr, target.data(), fstype, flags);
}

}  // namespace cinux::syscall
