/**
 * @file    user_access.cpp
 * @brief   The user copy door, with the demand pager behind it.
 *
 * The header's checks stay pure table walks; this translation unit
 * owns the copy itself. When the walk finds a page the active table
 * does not carry, the neutral demand pager gets the first refusal:
 * a promise the register still holds becomes a frame right here, so
 * a syscall read into a not-yet-touched bss page behaves exactly as
 * the fault path would have made the user's own store behave.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.2
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/user_access.hpp"

#include "kernel/arch/armv7/isr.hpp"
#include "kernel/arch/armv7/space_control.hpp"

#include "kernel/mm/address_space.hpp"
#include "kernel/mm/demand.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/mm/vmm.hpp"
#include "kernel/proc/task.hpp"
#include "kernel/syscall/syscall_internal.hpp"

namespace cinux::arch {

namespace {

/// Asks the neutral pager for every page the walk found missing. A
/// task without a register of promises keeps the old refusal: the
/// legacy lane maps everything it hands out up front.
bool ensure_paged(unsigned long address, unsigned long bytes, bool write) {
    proc::Task* const kSelf = cinux::syscall::CurrentTask();
    if (kSelf == nullptr || !kSelf->vmas.holds() || !kSelf->space.holds()) {
        return false;
    }
    const unsigned long long kCode = write ? isr::kFaultWasWrite : 0ULL;
    const unsigned long     kFirst = address & ~(page::kSize - 1UL);
    const unsigned long     kLast  = (address + bytes - 1UL) & ~(page::kSize - 1UL);
    for (unsigned long spot = kFirst; spot <= kLast; spot += page::kSize) {
        if (UserPageIsMapped(spot)) {
            continue;
        }
        if (mm::demand::FillPage(*kSelf->space, *kSelf->vmas, spot, kCode) ==
            mm::demand::FillOutcome::kUnhandled) {
            return false;
        }
    }
    return true;
}

}  // namespace

/// Runs the copy with the task's own tables live. The exit paths park
/// on the kernel root before a task's last word is written back, and a
/// user address has no meaning under those tables; the door therefore
/// lends the CPU the world the address belongs to and hands it back.
class SpaceLender {
public:
    explicit SpaceLender(proc::Task* task) : previous_(space::ActiveRoot()) {
        if (task != nullptr && task->space.holds() &&
            task->space->root() != previous_) {
            space::SwitchRoot(task->space->root());
            lent_ = true;
        }
    }
    ~SpaceLender() {
        if (lent_) {
            space::SwitchRoot(previous_);
        }
    }
    SpaceLender(const SpaceLender&)            = delete;
    SpaceLender& operator=(const SpaceLender&) = delete;

private:
    unsigned long previous_ = 0;
    bool          lent_     = false;
};

bool MoveUserBytes(unsigned long long destination, unsigned long long source,
                   unsigned long long bytes) {
    if (bytes == 0) {
        return true;
    }
    const SpaceLender kLender(cinux::syscall::CurrentTask());
    const unsigned long long kUserTop = space::kUserTop;
    const bool kDestUser   = destination <= kUserTop;
    const bool kSourceUser = source <= kUserTop;
    if (kDestUser == kSourceUser) {
        return false;  // kernel-to-kernel or user-to-user is not this door
    }
    const unsigned long user_address = static_cast<unsigned long>(kDestUser ? destination : source);
    if (!UserRangeIsMapped(user_address, static_cast<unsigned long>(bytes))) {
        // The pager may still hold a promise for the missing pages.
        if (!ensure_paged(user_address, static_cast<unsigned long>(bytes), kDestUser)) {
            return false;
        }
    }
    auto*       out = reinterpret_cast<unsigned char*>(destination);
    const auto* in  = reinterpret_cast<const unsigned char*>(source);
    for (unsigned long long index = 0; index < bytes; ++index) {
        out[index] = in[index];
    }
    return true;
}

}  // namespace cinux::arch
