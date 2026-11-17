#include "kernel/syscall/sys_fstat.hpp"
#include "kernel/syscall/sys_stat.hpp"

#include <cstdint>

#include "cinux/result.hpp"
#include "kernel/arch/abi.hpp"
#include "cinux/shared.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/fs/vfs.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/syscall_internal.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::syscall {

namespace {

/// One descriptor's record in the caller's shape: fstat asks for the
/// 32-bit stat, fstat64 for the wider one, and a caller reads whichever
/// it asked for.
long long fstat_into(const SyscallFrame& frame, cinux::arch::abi::StatLayout layout) {
    const arch::IrqGuard                           kGuard;
    const base::Result<base::Shared<fs::OpenFile>> kSlot = LiveSlot(frame.rdi);
    if (!kSlot.ok()) {
        return -kEbadf;
    }
    if (kSlot.value()->ops->stat == nullptr) {
        // A backend with no stat face has nothing honest to say; the
        // caller learns the table has no such shape rather than a
        // made-up record.
        return -kEnosys;
    }
    fs::FsStat               probed{};
    const base::Result<void> kProbed =
        kSlot.value()->ops->stat(kSlot.value()->backend, kSlot.value()->node, probed);
    if (!kProbed.ok()) {
        return -ErrnoFrom(kProbed.error());
    }
    unsigned char record[cinux::arch::abi::kStat64Layout.bytes] = {};
    WriteStatRecord(record, probed, layout);
    if (!uaccess::CopyToUser(frame.rsi, record, sizeof(record))) {
        return -kEfault;
    }
    return 0;
}

}  // namespace

long long HandleFstat(const SyscallFrame& frame) {
    return fstat_into(frame, cinux::arch::abi::kStatLayout);
}

long long HandleFstat64(const SyscallFrame& frame) {
    return fstat_into(frame, cinux::arch::abi::kStat64Layout);
}

}  // namespace cinux::syscall
