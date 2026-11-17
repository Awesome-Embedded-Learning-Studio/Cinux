/**
 * @file    fault_resolve.cpp
 * @brief   The seat's body: CoW first, then the register's promises.
 *
 * @author  Charliechen114514
 * @date    2026-10-15
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_proc
 * @copyright Copyright (c) 2026
 */

#include "kernel/proc/fault_resolve.hpp"

#include "kernel/arch/isr.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/mm/cow.hpp"
#include "kernel/mm/demand.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/task.hpp"
#include "kernel/syscall/signal.hpp"

namespace cinux::proc {

namespace {

bool resolve_current_fault(unsigned long long address, unsigned long long error_code) {
    const Task* const kSelf = Scheduler::self().current();
    if (kSelf == nullptr || !kSelf->space.holds() || !kSelf->vmas.holds()) {
        return false;
    }
    // Work on a copy: the region could be unregistered and freed by a
    // sibling thread the moment find() returns, so the terms are read
    // from a snapshot that owns its shared attachments.
    mm::VmaRegion              region{};
    const bool                 kCovered     = kSelf->vmas->lookup(address, region);
    const mm::VmaRegion* const kRegion      = kCovered ? &region : nullptr;
    const bool                 kWrite       = (error_code & arch::isr::kFaultWasWrite) != 0;
    const bool                 kInstruction = (error_code & arch::isr::kFaultInstruction) != 0;
    if ((error_code & arch::isr::kFaultReserved) != 0) {
        return false;
    }
    // The range's terms come first: a write the range never promised is
    // refused even when the leaf still carries a share mark from a fork.
    // Copy-on-write answers only the faults the range allows, so a
    // read-only promise costs the writer a fault, never a silent copy.
    const bool kAllowed = kRegion != nullptr && (!kWrite || kRegion->writable) &&
                          (!kInstruction || kRegion->executable) &&
                          (kWrite || kInstruction || kRegion->readable) &&
                          (kRegion->kind != mm::VmaKind::kStack || address >= kRegion->stack_floor);
    if (!kAllowed) {
        // Whether a fatal fault ends the task or unwinds a seated
        // episode is the port's business: this resolver answers the
        // repair question only, and the arch door that called it owns
        // the death (x86 exits the task, arm retires the episode).
        return false;
    }
    if (mm::cow::TryResolvePageFault(address, error_code)) {
        return true;
    }
    // kRetry counts as resolved: the page already stands mapped and the
    // fault was the CPU's spurious one, so the instruction just runs
    // again. Treating it as fatal is what killed tasks on their own
    // pages.
    const mm::demand::FillOutcome kFill =
        mm::demand::FillPage(*kSelf->space, *kSelf->vmas, address, error_code);
    if (kFill == mm::demand::FillOutcome::kFilled || kFill == mm::demand::FillOutcome::kRetry) {
        return true;
    }
    return false;
}

}  // namespace

void InstallFaultResolver() {
    arch::isr::InstallPageFaultResolver(&resolve_current_fault);
}

}  // namespace cinux::proc
