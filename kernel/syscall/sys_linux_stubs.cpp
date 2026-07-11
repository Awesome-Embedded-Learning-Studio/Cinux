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

#include "kernel/arch/x86_64/user_access.hpp"  // copy_to_user (sched_getaffinity mask)
#include "kernel/drivers/acpi/acpi.hpp"        // g_acpi_info (cpu_count)
#include "kernel/errno.hpp"
#include "kernel/proc/signal.hpp"  // signal_find_task_by_pid / signal_send (tkill)

namespace cinux::syscall {

// getcpu(309): glibc probes it for per-CPU affinity hints via vDSO; the syscall
// fallback returning -ENOSYS makes glibc give up the hint gracefully.
int64_t sys_getcpu(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) {
    return -cinux::kEnosys;
}

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

// tkill(200): send a signal to a task by tid.  busybox sh's job control uses
// this to forward SIGINT (Ctrl+C from the PTY) to the foreground child (e.g.
// ping).  Cinux tasks are single-threaded (tid == pid), so reuse the same
// pid->Task lookup + signal_send path as sys_kill(62).
int64_t sys_tkill(uint64_t tid, uint64_t sig, uint64_t, uint64_t, uint64_t, uint64_t) {
    auto* t = cinux::proc::signal_find_task_by_pid(static_cast<int>(tid));
    if (t == nullptr) {
        return -cinux::kEsrch;
    }
    return cinux::proc::signal_send(t, static_cast<cinux::proc::Signal>(sig));
}

// setitimer(38): busybox ping uses ITIMER_REAL to fire SIGALRM every second
// (send next echo + per-packet timeout).  We don't run a real itimer, so accept
// the call as a no-op (ping falls back to blocking recv; Ctrl+C interrupts).
// A faithful itimer -> SIGALRM is a follow-up (needs kernel timer + signal).
int64_t sys_setitimer(uint64_t /*which*/, uint64_t /*new_value*/, uint64_t /*old_value*/,
                       uint64_t, uint64_t, uint64_t) {
    return 0;
}

// sched_getaffinity(204): busybox `nproc` + glibc probe it to learn the online
// CPU set.  Cinux marks every online CPU (BSP + APs from g_acpi_info) eligible,
// so the mask has cpu_count low bits set and nproc reports the real topology.
// Returns the byte count placed (Linux raw-syscall contract; glibc/musl wrappers
// translate non-negative to 0).  pid is ignored -- Cinux has one global affinity.
int64_t sys_sched_getaffinity(uint64_t /*pid*/, uint64_t cpusetsize, uint64_t mask_virt,
                               uint64_t, uint64_t, uint64_t) {
    if (mask_virt == 0 || cpusetsize == 0) {
        return -cinux::kEfault;
    }
    const auto& info = cinux::drivers::acpi::g_acpi_info;
    uint32_t    n    = info.cpu_count > 0 ? info.cpu_count : 1;
    uint8_t     buf[128] = {0};  // up to 1024 CPUs
    for (uint32_t i = 0; i < n && i < sizeof(buf) * 8; ++i) {
        buf[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
    }
    uint64_t bytes = (static_cast<uint64_t>(n) + 7) / 8;
    if (bytes > cpusetsize) {
        bytes = cpusetsize;
    }
    if (!cinux::user::copy_to_user(reinterpret_cast<void*>(mask_virt), buf, bytes)) {
        return -cinux::kEfault;
    }
    return static_cast<int64_t>(bytes);
}

}  // namespace cinux::syscall
