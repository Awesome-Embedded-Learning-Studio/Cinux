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

int64_t do_mount_kernel(const char* source, const char* target, const char* fstype,
                        uint64_t flags) {
    (void)source;  // tmpfs has no backing device
    (void)flags;   // MS_* mount flags not yet modelled

    if (target == nullptr || fstype == nullptr || target[0] == '\0') {
        return -kEinval;
    }

    // fstype factory.  Only tmpfs is mountable at runtime today; anything else
    // is -ENODEV (Linux: "no such device" for an unknown filesystem type).
    FileSystem* fs = nullptr;
    if (strcmp(fstype, "tmpfs") == 0) {
        auto* tfs = new TmpFs();
        auto  m   = tfs->mount();
        if (!m.ok()) {
            kprintf("[SYS_MOUNT] tmpfs mount() failed: %s\n", cinux::lib::error_string(m.error()));
            delete tfs;
            return -to_errno(m.error());
        }
        fs = tfs;
    } else {
        kprintf("[SYS_MOUNT] unknown filesystem type '%s'\n", fstype);
        return -kEnodev;
    }

    // owned=true: sys_umount2 will delete this backend when the mount comes down.
    if (!vfs_mount_add(target, fs, /*owned=*/true)) {
        kprintf("[SYS_MOUNT] mount table full, cannot mount '%s'\n", target);
        delete fs;  // reclaim the just-allocated backend
        return -kEnomem;
    }
    return 0;
}

int64_t sys_mount(uint64_t source_virt, uint64_t target_virt, uint64_t fstype_virt, uint64_t flags,
                  uint64_t data, uint64_t) {
    (void)data;  // mount options string -- not yet parsed

    // source is unused for tmpfs; do not even read it (tolerates a NULL source,
    // which Linux permits for source-less filesystems like tmpfs).
    (void)source_virt;

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
