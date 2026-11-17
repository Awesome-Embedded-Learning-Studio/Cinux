#include "kernel/arch/armv7/task_switch.hpp"

#include "kernel/arch/context.hpp"
#include "kernel/arch/per_cpu.hpp"
#include "kernel/boot/print.hpp"

namespace cinux::armv7 {

namespace {

constexpr unsigned long kSteps = 3;
constexpr unsigned long kMainSlot = ~0UL;

struct MiniTask {
    cinux::arch::CpuContext context;
    unsigned char*          stack;
    unsigned long           steps;
};

MiniTask g_tasks[2];
alignas(8) unsigned char g_stacks[2][4096];
unsigned long g_current = kMainSlot;
cinux::arch::CpuContext g_caller;

void yield_to_other() {
    const unsigned long from = g_current;
    const unsigned long to   = from == 0 ? 1 : 0;
    g_current                = to;
    cinux::arch::per_cpu::self().current_slot = to;
    cinux::arch::ContextSwitch(&g_tasks[from].context, &g_tasks[to].context);
}

void finish_current() {
    const unsigned long from = g_current;
    g_current                = kMainSlot;
    cinux::arch::per_cpu::self().current_slot = kMainSlot;
    cinux::arch::ContextSwitch(&g_tasks[from].context, &g_caller);
}

void task_body(unsigned long slot) {
    while (true) {
        MiniTask& task = g_tasks[slot];
        if (task.steps >= kSteps) {
            cinux::print::Println("[armv7] task %u finished", slot);
            finish_current();
        }
        ++task.steps;
        cinux::print::Println("[armv7] task %u step %u", slot, task.steps);
        yield_to_other();
    }
}

extern "C" void task_entry_zero() {
    task_body(0);
}

extern "C" void task_entry_one() {
    task_body(1);
}

}  // namespace

bool RunTaskSelfCheck() {
    g_current = 0;
    for (unsigned long slot = 0; slot < 2; ++slot) {
        g_tasks[slot]             = MiniTask{};
        g_tasks[slot].stack       = g_stacks[slot];
        g_tasks[slot].context.sp  = reinterpret_cast<unsigned long>(g_stacks[slot]) + 4096UL;
        g_tasks[slot].context.sp &= ~7UL;
        g_tasks[slot].context.lr  = reinterpret_cast<unsigned long>(
            slot == 0 ? &task_entry_zero : &task_entry_one);
    }
    cinux::arch::per_cpu::self().current_slot = 0;
    cinux::arch::ContextSwitch(&g_caller, &g_tasks[0].context);
    return g_tasks[0].steps == kSteps && g_tasks[1].steps == kSteps;
}

}  // namespace cinux::armv7
