#include "kernel/syscall/sys_stat.hpp"

#include <cstdint>

#include "cinux/result.hpp"
#include "kernel/arch/abi.hpp"
#include "cinux/shared.hpp"
#include "kernel/fs/file_world.hpp"
#include "kernel/fs/kernel_vfs.hpp"
#include "kernel/fs/fs_config.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/syscall_internal.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::syscall {

long long HandleStat(const SyscallFrame& frame) {
    if (!UserRangeWritable(frame.rsi, arch::abi::kStatLayout.bytes)) {
        return -kEfault;
    }
    char storage[fs::kFsPathMax] = {};
    if (const long long kBad = FoldUserPath(frame.rdi, storage, fs::kFsPathMax); kBad < 0) {
        return kBad;
    }
    return StatPath(storage, frame.rsi);
}

long long StatPathInto(const char* path, Word destination, arch::abi::StatLayout layout) {
    if (!UserRangeWritable(destination, layout.bytes)) {
        return -kEfault;
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
    const base::Result<void> kProbed = flat.ops->stat(flat.self, kNode.value(), probed);
    if (!kProbed.ok()) {
        return -ErrnoFrom(kProbed.error());
    }
    unsigned char record[arch::abi::kStatLayout.bytes] = {};
    if (layout.bytes > sizeof(record)) {
        unsigned char wide[arch::abi::kStat64Layout.bytes] = {};
        WriteStatRecord(wide, probed, layout);
        if (!uaccess::CopyToUser(destination, wide, layout.bytes)) {
            return -kEfault;
        }
        return 0;
    }
    WriteStatRecord(record, probed, layout);
    if (!uaccess::CopyToUser(destination, record, sizeof(record))) {
        return -kEfault;
    }
    return 0;
}

long long StatPath(const char* path, unsigned long long destination) {
    return StatPathInto(path, destination, arch::abi::kStatLayout);
}

long long HandleStat64(const SyscallFrame& frame) {
    if (!UserRangeWritable(frame.rsi, arch::abi::kStat64Layout.bytes)) {
        return -kEfault;
    }
    char storage[fs::kFsPathMax] = {};
    if (const long long kBad = FoldUserPath(frame.rdi, storage, fs::kFsPathMax); kBad < 0) {
        return kBad;
    }
    return StatPathInto(storage, frame.rsi, arch::abi::kStat64Layout);
}

long long StatPathAt(Word descriptor, const char* relative, Word destination) {
    if (!UserRangeWritable(destination, arch::abi::kStatLayout.bytes)) {
        return -kEfault;
    }
    const base::Result<base::Shared<fs::OpenFile>> kDir = LiveSlot(descriptor);
    if (!kDir.ok()) {
        return -kEbadf;
    }
    const base::Shared<fs::OpenFile>& anchor = kDir.value();
    if (anchor->node.type != fs::InodeType::kDirectory) {
        return -kEnotdir;
    }
    const base::Result<fs::FsNode> kNode =
        anchor->ops->lookup_at(anchor->backend, anchor->node, relative);
    if (!kNode.ok()) {
        return kNode.error() == base::KernelError::kNotSupported ? -kEopnotsupp
                                                                 : -ErrnoFrom(kNode.error());
    }
    fs::FsStat               probed{};
    const base::Result<void> kProbed = anchor->ops->stat(anchor->backend, kNode.value(), probed);
    anchor->ops->release(anchor->backend, kNode.value());
    if (!kProbed.ok()) {
        return -ErrnoFrom(kProbed.error());
    }
    SyscallStat sink{};
    sink.ino   = probed.ino;
    sink.mode  = probed.mode;
    sink.links = probed.links;
    sink.size  = static_cast<std::int64_t>(probed.size);
    sink.rdev  = probed.rdev;
    sink.mtime = probed.mtime;
    if (!uaccess::CopyToUser(destination, &sink, sizeof(sink))) {
        return -kEfault;
    }
    return 0;
}


}  // namespace cinux::syscall
