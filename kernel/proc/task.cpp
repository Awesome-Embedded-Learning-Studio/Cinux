#include "kernel/arch/task_context.hpp"
#include "kernel/proc/task.hpp"

#include "cinux/memory.hpp"
#include "cinux/ptr.hpp"
#include "kernel/arch/context.hpp"
#include "kernel/proc/proc_config.hpp"

namespace cinux::proc {

void PrepareTaskContext(cinux::arch::CpuContext& ctx, const StartPlan& plan) {
    auto* const kBottomWord = cinux::base::PtrAt<unsigned long long>(plan.stack_bottom);
    *kBottomWord            = kStackMagic;
    cinux::arch::ArmTaskContext(ctx, plan.trampoline, plan.entry, plan.exit_hook,
                                plan.stack_bottom, plan.stack_top);
}

}  // namespace cinux::proc
