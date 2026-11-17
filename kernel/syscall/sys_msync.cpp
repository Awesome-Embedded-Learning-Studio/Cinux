/**
 * @file    sys_msync.cpp
 * @brief   msync(2): the write-back promise over one mapped span.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_syscall
 * @copyright Copyright (c) 2026
 */

#include "kernel/syscall/sys_msync.hpp"

#include "cinux/math.hpp"
#include "cinux/result.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/proc/task.hpp"
#include "kernel/syscall/syscall_internal.hpp"

namespace cinux::syscall {

namespace {

/// Linux's three msync flags: sync flush, async hint, invalidate.
constexpr unsigned long kMsSync      = 1U << 0U;
constexpr unsigned long kMsAsync     = 1U << 1U;
constexpr unsigned long kMsInvalidate = 1U << 2U;

}  // namespace

long long HandleMsync(const SyscallFrame& frame) {
    const proc::Task* const kSelf = CurrentTask();
    if (kSelf == nullptr || !kSelf->vmas.holds()) {
        return -kEnomem;
    }
    if ((frame.rdx & ~(kMsSync | kMsAsync | kMsInvalidate)) != 0) {
        return -kEinval;
    }
    if (frame.rdi >= kUserAddressLimit || frame.rsi > kUserAddressLimit - frame.rdi) {
        return -kEnomem;
    }
    if (frame.rsi == 0) {
        return 0;
    }
    // The promise is checked page by page the way Linux reads it: any
    // page of the span the range register does not cover names an
    // unmapped promise, which is ENOMEM, not a flush owed.
    const unsigned long kEnd =
        cinux::base::math::AlignUp(frame.rdi + frame.rsi, cinux::arch::page::kSize);
    for (unsigned long spot = frame.rdi & ~(cinux::arch::page::kSize - 1U); spot < kEnd;
         spot += cinux::arch::page::kSize) {
        mm::VmaRegion region{};
        if (!kSelf->vmas->lookup(spot, region)) {
            return -kEnomem;
        }
    }
    // Every mapping this kernel shapes is coherent as written: device
    // frames are the device's own memory, private copies live in the
    // page frames the CPU already sees. The write-back promise holds,
    // so the honest answer is that nothing is owed.
    return 0;
}

}  // namespace cinux::syscall
