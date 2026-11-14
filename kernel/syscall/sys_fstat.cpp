#include "kernel/boot/print.hpp"
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

long long HandleFstat(const SyscallFrame& frame) {
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
#include "kernel/boot/print.hpp"
    unsigned char record[arch::abi::kStatLayout.bytes] = {};
    WriteStatRecord(record, probed);
    if (!uaccess::CopyToUser(frame.rsi, record, sizeof(record))) {
        return -kEfault;
    }
    return 0;
}

}  // namespace cinux::syscall
