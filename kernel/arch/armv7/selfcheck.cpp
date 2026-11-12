#include "kernel/arch/armv7/selfcheck.hpp"

#include "kernel/arch/space_control.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/arch/context.hpp"
#include "kernel/arch/kernel_stack.hpp"
#include "kernel/arch/per_cpu.hpp"
#include "kernel/mm/vmm.hpp"
#include "kernel/mm/pmm.hpp"

namespace cinux::armv7 {

bool RunPmmSelfCheck() {
    auto& pmm = cinux::mm::Pmm::self();
    const unsigned long before = pmm.free_page_count();

    const cinux::base::PhysAddr first = pmm.allocate_page();
    if (first.raw == 0) {
        return false;
    }
    const cinux::base::PhysAddr second = pmm.allocate_page();
    if (second.raw == 0 || second.raw == first.raw) {
        pmm.free_page(first);
        return false;
    }

    // Identity-mapped: the physical address is its own virtual address.
    auto* const page = reinterpret_cast<volatile unsigned long*>(first.raw);
    *page            = 0x5A5A5A5AUL;
    if (*page != 0x5A5A5A5AUL) {
        pmm.free_page(first);
        pmm.free_page(second);
        return false;
    }

    pmm.free_page(first);
    pmm.free_page(second);
    if (pmm.free_page_count() != before) {
        return false;
    }

    // An order above kMaxOrder must be refused, not served.
    return pmm.allocate_pages(20).raw == 0;
}

namespace {
alignas(16) unsigned char g_heap_arena[64 * 1024];
}  // namespace

bool RunHeapSelfCheck() {
    auto& heap = cinux::mm::Heap::self();
    if (!heap.init(reinterpret_cast<unsigned long>(g_heap_arena), sizeof(g_heap_arena))) {
        return false;
    }
    const unsigned long free_at_start = heap.free_bytes();

    auto* const first  = static_cast<unsigned char*>(heap.allocate(128));
    auto* const second = static_cast<unsigned char*>(heap.allocate(256));
    if (first == nullptr || second == nullptr || first == second) {
        return false;
    }
    if ((reinterpret_cast<unsigned long>(first) % 16U) != 0) {
        return false;
    }
    first[0]   = 0x11;
    second[0]  = 0x22;
    if (first[0] != 0x11 || second[0] != 0x22) {
        return false;
    }
    if (!heap.free(first) || !heap.free(second)) {
        return false;
    }
    if (heap.free_bytes() != free_at_start) {
        return false;
    }
    // Double free must be refused rather than corrupting the list.
    return !heap.free(first);
}

bool RunMapSelfCheck() {
    cinux::mm::KernelTables tables;
    const unsigned long      root = cinux::arch::space::ActiveRoot();
    const auto               frame = cinux::mm::Pmm::self().allocate_page();
    if (frame.raw == 0) {
        return false;
    }
    constexpr unsigned long kSpot = 0x50000000UL;
    if (!cinux::mm::walk::MapPage(tables, root, kSpot, frame.raw)) {
        return false;
    }
    auto* const window = reinterpret_cast<volatile unsigned long*>(kSpot);
    *window            = 0xA5A5A5A5UL;
    return *window == 0xA5A5A5A5UL;
}

namespace {

cinux::arch::CpuContext  g_ctx_main;
cinux::arch::CpuContext  g_ctx_other;
cinux::arch::CpuContext* g_main_pointer = nullptr;
alignas(8) unsigned char  g_other_stack[4096];
volatile bool             g_other_ran = false;

/// The other world: note that it ran, then hand the CPU back.
extern "C" void context_other_entry() {
    g_other_ran = true;
    cinux::arch::ContextSwitch(&g_ctx_other, g_main_pointer);
}

}  // namespace

bool RunContextSelfCheck() {
    g_other_ran    = false;
    g_ctx_other    = cinux::arch::CpuContext{};
    g_ctx_other.sp = reinterpret_cast<unsigned long>(g_other_stack) + sizeof(g_other_stack);
    g_ctx_other.sp &= ~7UL;
    g_ctx_other.lr = reinterpret_cast<unsigned long>(&context_other_entry);
    g_main_pointer = &g_ctx_main;

    cinux::arch::ContextSwitch(&g_ctx_main, &g_ctx_other);
    return g_other_ran;
}

bool RunPerCpuSelfCheck() {
    static cinux::arch::per_cpu::PerCpu block{};
    block.cpu_id           = 0x5A5AUL;
    block.kernel_stack_top = 0x80001000UL;
    cinux::arch::per_cpu::Install(&block);
    const auto& read_back = cinux::arch::per_cpu::self();
    if (read_back.cpu_id != 0x5A5AUL || read_back.kernel_stack_top != 0x80001000UL) {
        return false;
    }
    cinux::arch::SetKernelStackTop(0x80002000UL);
    if (cinux::arch::per_cpu::self().kernel_stack_top != 0x80002000UL) {
        return false;
    }
    return cinux::arch::CurrentKernelStack() != 0;
}

}  // namespace cinux::armv7
