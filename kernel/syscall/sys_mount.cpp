/**
 * @file kernel/syscall/sys_mount.cpp
 * @brief sys_mount handler implementation (F6-M1)
 *
 * fstype-driven filesystem factory: "tmpfs" -> a heap TmpFs (mounted and
 * registered as owned, so sys_umount2 reclaims it).  Other fstypes yield
 * -ENODEV.  The @p source and @p flags (MS_*) arguments are accepted for Linux
 * ABI parity but unused -- tmpfs has no backing device and CinuxOS does not yet
 * model mount flags.  This is the runtime mount path; the boot /tmp mount uses
 * the static g_tmpfs via tmpfs::init() (tmpfs_init.cpp), registered unowned.
 */

#include "kernel/syscall/sys_mount.hpp"

#include <stdint.h>

#include <memory>  // std::make_unique (RAII over raw new/delete; EXEMPT-reviewed)

#include "kernel/errno.hpp"
#include "kernel/fs/path.hpp"         // PathBuf
#include "kernel/fs/tmpfs/tmpfs.hpp"  // TmpFs (the one supported runtime fstype)
#include "kernel/fs/vfs_mount.hpp"    // vfs_mount_add
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
    // tmpfs has no backing device
    // MS_* mount flags not yet modelled

    if (target == nullptr || fstype == nullptr || target[0] == '\0') {
        return -kEinval;
    }

    // fstype factory.  Only tmpfs is mountable at runtime today; anything else
    // is -ENODEV (Linux: "no such device" for an unknown filesystem type).
    if (strcmp(fstype, "tmpfs") != 0) {
        kprintf("[SYS_MOUNT] unknown filesystem type '%s'\n", fstype);
        return -kEnodev;
    }

    // unique_ptr owns the TmpFs until mount() succeeds; on failure it is
    // auto-freed without a manual delete on every error leg.
    std::unique_ptr<TmpFs> tfs(new TmpFs());
    auto                   m = tfs->mount();
    if (!m.ok()) {
        kprintf("[SYS_MOUNT] tmpfs mount() failed: %s\n", cinux::lib::error_string(m.error()));
        return -to_errno(m.error());
    }

    // Hand ownership to the mount table (owned=true: sys_umount2 deletes on unmount).
    FileSystem* fs = tfs.release();
    if (!vfs_mount_add(target, fs, /*owned=*/true)) {
        kprintf("[SYS_MOUNT] mount table full, cannot mount '%s'\n", target);
        delete fs;  // mount table did not take ownership; reclaim
        return -kEnomem;
    }
    return 0;
}

int64_t sys_mount([[maybe_unused]] uint64_t source_virt, uint64_t target_virt, uint64_t fstype_virt, uint64_t flags,
                  [[maybe_unused]] uint64_t data, uint64_t) {
    // mount options string -- not yet parsed

    // source is unused for tmpfs; do not even read it (tolerates a NULL source,
    // which Linux permits for source-less filesystems like tmpfs).

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
