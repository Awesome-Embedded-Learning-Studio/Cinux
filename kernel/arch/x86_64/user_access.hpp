/**
 * @file kernel/arch/x86_64/user_access.hpp
 * @brief SMAP-aware user-memory accessors (Linux uaccess.h aligned)
 *
 * CinuxOS SMAP model (see document/ai for the design that replaced F9 batch 4's
 * broken global-STAC approach):
 *   - CR4.SMAP is set, so by default (RFLAGS.AC = 0) the kernel CANNOT read or
 *     write user pages. Any direct dereference of a user pointer from kernel
 *     code is a bug and will #PF.
 *   - Kernel code that legitimately touches user memory (syscalls reading
 *     user args / writing user results, signal frame delivery, execve reading
 *     argv/envp) MUST go through these accessors. Each accessor raises AC only
 *     for the copy window via stac(), then clears it with clac().
 *   - RFLAGS.AC is a per-CPU bit and context_switch does NOT save RFLAGS, so an
 *     accessor window must NEVER block/schedule. Callers that need to block
 *     (blocking read, waitpid) stage data in a kernel buffer while blocked and
 *     copy_to_user() only after they are runnable again.
 *
 * Fault handling: CinuxOS has no exception-table infrastructure, so unlike
 * Linux's copy_to_user (which truncates the copy and returns -EFAULT on a
 * genuine mid-copy fault via _ASM_EXTABLE_UA), these accessors rely on the
 * demand-paging contract: a not-present user page mid-copy is served by the
 * #PF handler (AC is 1 inside the window), so the byte loop either completes or
 * hits a genuinely unmappable address (which the handler turns into SIGSEGV /
 * panic as appropriate). access_ok() rejects bad ranges up front.
 *
 * Namespace: cinux::arch (stac/clac hardware primitives) + cinux::user
 * (access_ok / copy_to_from_user / put_user / get_user).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "kernel/arch/x86_64/paging_config.hpp"  // is_user_vaddr

namespace cinux::arch {

/// Set RFLAGS.AC = 1, allowing kernel access to user pages (SMAP bypass for the
/// accessor window). Per-CPU; pair with clac() and never block in between.
#ifdef CINUX_HOST_TEST
inline void stac() {}  // host unit tests have no SMAP; no-op keeps RFLAGS clean
inline void clac() {}
#else
inline void stac() {
    __asm__ volatile("stac" ::: "memory");
}
inline void clac() {
    __asm__ volatile("clac" ::: "memory");
}
#endif

}  // namespace cinux::arch

namespace cinux::user {

/// True iff [addr, addr+size) lies entirely in the user canonical half
/// (bit 47 = 0). A pure range check -- no fault, no page walk. Rejects NULL,
/// any kernel high-half address, and addr+size wraparound. Stricter than
/// cinux::syscall::validate_user_ptr (which only checks canonical form and
/// admits kernel high-half addresses).
inline bool access_ok(const void* addr, size_t size) {
    uint64_t a = reinterpret_cast<uint64_t>(addr);
    if (a == 0) {
        return false;  // NULL is never a valid user range
    }
    uint64_t end;
    if (__builtin_add_overflow(a, size, &end)) {
        return false;  // addr + size wraps past 2^64
    }
    // end is one-past-the-last byte; the last byte touched is end-1 (or a when
    // size == 0). Both endpoints must be in the user half.
    uint64_t last = (size == 0) ? a : end - 1;
    return cinux::arch::is_user_vaddr(a) && cinux::arch::is_user_vaddr(last);
}

/// Copy @p n bytes kernel -> user. Returns true on success; false (caller
/// returns -EFAULT) if access_ok rejects the range. The stac/clac window is a
/// tight byte loop that never blocks.
inline bool copy_to_user(void* dst_user, const void* src_kernel, size_t n) {
    if (!access_ok(dst_user, n)) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    cinux::arch::stac();
    auto*       d = reinterpret_cast<uint8_t*>(dst_user);
    const auto* s = reinterpret_cast<const uint8_t*>(src_kernel);
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    cinux::arch::clac();
    return true;
}

/// Copy @p n bytes user -> kernel. Same contract as copy_to_user.
inline bool copy_from_user(void* dst_kernel, const void* src_user, size_t n) {
    if (!access_ok(src_user, n)) {
        return false;
    }
    if (n == 0) {
        return true;
    }
    cinux::arch::stac();
    auto*       d = reinterpret_cast<uint8_t*>(dst_kernel);
    const auto* s = reinterpret_cast<const uint8_t*>(src_user);
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    cinux::arch::clac();
    return true;
}

/// Write a single typed value to user memory (Linux put_user).
template <typename T>
inline bool put_user(T value, T* dst_user) {
    return copy_to_user(dst_user, &value, sizeof(T));
}

/// Read a single typed value from user memory (Linux get_user).
template <typename T>
inline bool get_user(T* dst_kernel, const T* src_user) {
    return copy_from_user(dst_kernel, src_user, sizeof(T));
}

}  // namespace cinux::user
