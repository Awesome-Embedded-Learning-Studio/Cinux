#include "kernel/syscall/sys_sync.hpp"

#include "kernel/fs/kernel_vfs.hpp"
#include "kernel/syscall/syscall.hpp"

namespace cinux::syscall {

long long HandleSync(const SyscallFrame& /*frame*/) {
    return fs::kernel_vfs().sync_all() ? 0 : -kEio;
}

}
