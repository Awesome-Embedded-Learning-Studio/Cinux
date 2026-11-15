/**
 * @file    vmm_bringup.cpp
 * @brief   The armv7 side of the memory subsystem's bring-up: build the
 *          first-level table, wire the demo second level, turn the MMU on.
 *
 * The shared walk does the per-page work; this file owns the shapes the
 * walk cannot know: which sections are device memory, which are RAM from
 * the firmware's memory map, and where the first-level table lives.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/mm/vmm.hpp"

#include <cstdint>

#include "cinux/addr.hpp"
#include "cinux/assert.hpp"
#include "cinux/math.hpp"
#include "cinux/ptr.hpp"
#include "kernel/arch/armv7/page_entry.hpp"
#include "kernel/arch/armv7/page_walk_shape.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/arch/space.hpp"
#include "kernel/boot/boot_info.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/pmm.hpp"

namespace cinux::mm {

namespace {

using cinux::arch::page::Entry;

/// One section descriptor per MiB; the table must be 16 KiB aligned.
alignas(16384) Entry g_l1[4096];
/// The demo window's second level.
alignas(1024) Entry g_l2[256];
alignas(4096) unsigned char g_demo_page[4096];

unsigned long g_root = 0;

constexpr std::uint32_t kSection = 0x2U;
constexpr std::uint32_t kDeviceTerms = kSection | (1U << 2U) | (1U << 4U) | (0x1U << 10U);
constexpr std::uint32_t kNormalTerms =
    kSection | (1U << 2U) | (1U << 3U) | (1U << 12U) | (0x1U << 10U);

void InvalidateTlb() {
    asm volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0));
    asm volatile("dsb");
    asm volatile("isb");
}

}  // namespace

unsigned long KernelTables::take_root() {
    // The first-level table is sixteen contiguous KiB on a 16 KiB
    // boundary. Take eight contiguous pages and use the aligned window;
    // the pages outside it are leaked on purpose.
    const auto kBlock = Pmm::self().allocate_pages(3);
    if (kBlock.raw == 0) {
        return 0;
    }
    const unsigned long kSkip = ((0x4000UL - (kBlock.raw & 0x3FFFUL)) & 0x3FFFUL) / 4096UL;
    if (kSkip > 4) {
        return 0;
    }
    const unsigned long kRoot = kBlock.raw + (kSkip * 4096UL);
    // The window is four pages; hand the rest of the block straight back
    // so a later release of four pages leaves the ledger where it was.
    for (unsigned long page = 0; page < 8; ++page) {
        if (page < kSkip || page >= kSkip + 4) {
            Pmm::self().free_page(
                cinux::base::PhysAddr{kBlock.raw + (page * 4096UL)});
        }
    }
    auto* const table = cinux::base::PtrAt<unsigned char>(kRoot);
    for (unsigned long offset = 0; offset < 0x4000UL; ++offset) {
        table[offset] = 0;
    }
    return kRoot;
}

void KernelTables::release_root(unsigned long physical) {
    // take_root handed out a four-page window inside an aligned block;
    // the pages outside the window were leaked on purpose there.
    for (unsigned long page = 0; page < 4; ++page) {
        Pmm::self().free_page(base::PhysAddr{physical + (page * 4096UL)});
    }
}

void MapFramebufferDoor(const cinux::boot::BootInfo& info) {
    static_cast<void>(info);  // No framebuffer door on this board yet.
}

bool MapDeviceWindow(std::uint64_t physical, unsigned long bytes) {
    // This port needs no ioremap door: the board's device windows are
    // section-mapped identity at bring-up, so the drivers that ask for one
    // on x86 reach these addresses directly here. The refusal is honest --
    // the old body mapped the physical address as a virtual one against
    // freshly drawn frames, a window nothing could have wanted.
    static_cast<void>(physical);
    static_cast<void>(bytes);
    return false;
}

const cinux::boot::BootInfo* BringUpAddressSpace(const cinux::boot::BootInfo& handoff) {
    for (unsigned long index = 0; index < 4096; ++index) {
        g_l1[index] = Entry{0};
    }
    // The board's device windows live in the low 512 MiB.
    for (unsigned long index = 0; index < 512; ++index) {
        g_l1[index] = Entry{static_cast<std::uint32_t>(index << 20U) | kDeviceTerms};
    }
    // RAM comes from the firmware's map, a section at a time.
    for (std::uint32_t entry = 0; entry < handoff.e820_count; ++entry) {
        if (handoff.e820[entry].type != cinux::boot::kE820Usable) {
            continue;
        }
        const auto kBase = static_cast<unsigned long>(handoff.e820[entry].base);
        const auto kTop  = kBase + static_cast<unsigned long>(handoff.e820[entry].length);
        for (unsigned long window = kBase & ~0xFFFFFUL; window < kTop; window += 0x100000UL) {
            g_l1[window >> 20U] = Entry{static_cast<std::uint32_t>(window) | kNormalTerms};
        }
    }
    // The demo window: one second-level table with one small page.
    for (unsigned long index = 0; index < 256; ++index) {
        g_l2[index] = Entry{0};
    }
    g_l2[0] = cinux::arch::page::MakePageEntry(
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(g_demo_page)),
        cinux::arch::page::kUser);
    g_l1[0x40000000UL >> 20U] = cinux::arch::page::MakeTablePointer(
        static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(g_l2)), Entry{0});

    // The heap window's second levels are born here, under the kernel
    // root, before any task space copies the kernel half: every megabyte
    // the heap later claims needs a new first-level slot, and a slot
    // born at growth time would live only in whichever root was loaded
    // then. Pre-created tables are shared by that copy, so growth edits
    // the same second level whichever root walks it.
    KernelTables tables;
    const unsigned long kHeapTop =
        cinux::arch::space::kHeapBase + cinux::arch::space::kHeapReserveBytes;
    for (unsigned long window = cinux::arch::space::kHeapBase; window < kHeapTop;
         window += 0x100000UL) {
        const unsigned long kSecond = tables.take_page();
        if (kSecond == 0) {
            return nullptr;
        }
        g_l1[window >> 20U] = cinux::arch::page::MakeTablePointer(kSecond, Entry{0});
    }

    g_root = static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(g_l1));
    asm volatile("mcr p15, 0, %0, c3, c0, 0" : : "r"(0x55555555U));  // DACR
    asm volatile("mcr p15, 0, %0, c2, c0, 2" : : "r"(0));            // TTBCR
    asm volatile("mcr p15, 0, %0, c2, c0, 0" : : "r"(g_root));       // TTBR0
    InvalidateTlb();
    std::uint32_t sctlr = 0;
    asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr |= 1U;
    asm volatile("mcr p15, 0, %0, c1, c0, 0" : : "r"(sctlr));
    asm volatile("isb");
    return &handoff;
}

unsigned long KernelPageRoot() {
    return g_root;
}

}  // namespace cinux::mm

namespace cinux::armv7 {

bool TwoLevelDemo() {
    auto* const window = reinterpret_cast<volatile unsigned char*>(0x40000000UL);
    cinux::mm::g_demo_page[0] = 0;
    window[0]                 = 0x5A;
    return cinux::mm::g_demo_page[0] == 0x5A;
}

}  // namespace cinux::armv7
