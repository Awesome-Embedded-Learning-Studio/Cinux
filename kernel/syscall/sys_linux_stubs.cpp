/**
 * @file kernel/syscall/sys_linux_stubs.cpp
 * @brief Linux ABI probing stubs (gcc/g++ self-host syscall batch, 2026-07-05)
 *
 * See the header for the rationale.  Each handler returns just enough for the
 * probing libc to fall back gracefully:
 *   - rseq(334)    -> -ENOSYS: glibc gives up the restartable-sequence path.
 *   - clone3(435)  -> -ENOSYS: glibc falls back to clone/fork.
 *   - set_robust_list(273) -> 0: the robust-futex probe is satisfied.  We do
 *     not truly clean up robust locks on exit, but no compile/load path uses
 *     them (pthread-only); a future pthread batch would wire the cleanup.
 *   - sendfile(40) -> -ENOSYS: cp/copy tools fall back to read+write.
 */

#include "kernel/syscall/sys_linux_stubs.hpp"

#include "kernel/errno.hpp"

namespace cinux::syscall {

int64_t sys_rseq(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    return -cinux::kEnosys;
}

int64_t sys_clone3(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    return -cinux::kEnosys;
}

int64_t sys_set_robust_list(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    return 0;
}

int64_t sys_sendfile(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    return -cinux::kEnosys;
}

}  // namespace cinux::syscall
