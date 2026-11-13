/**
 * @file    cow_resolve.cpp
 * @brief   The x86_64 side of the neutral copy-on-write contract.
 *
 * The share mark is a software bit in the leaf plus one frame retain,
 * and the write-fault repair either hands the last sharer its write
 * permission back or copies the page for everyone else. The clone walk
 * itself is neutral and lives in mm/cow.cpp.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "cinux/assert.hpp"
#include "cinux/memory.hpp"
#include "cinux/ptr.hpp"
#include "cinux/spin_mutex.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/arch/isr.hpp"  // NOLINT(misc-include-cleaner) fault-code words shared with the reporter
#include "kernel/arch/page.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/space_control.hpp"
#include "kernel/mm/cow.hpp"
#include "kernel/mm/hardware_frames.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vmm.hpp"

namespace cinux::mm::cow {

namespace {

using cinux::arch::page::Entry;
using walk::FindSlot;
using walk::SlotAt;

base::SpinMutex g_fault_gate;

unsigned long long release_share(unsigned long long frame) {
    return Pmm::self().release_frame(cinux::base::PhysAddr{frame});
}

Entry shared_flags(const Entry& original) {
    Entry flags = cinux::arch::page::kCow;
    if (original.has(cinux::arch::page::kUser)) {
        flags = flags | cinux::arch::page::kUser;
    }
    if (original.has(cinux::arch::page::kNoExec)) {
        flags = flags | cinux::arch::page::kNoExec;
    }
    return flags;
}

Entry private_flags(const Entry& original, bool writable) {
    return cinux::arch::page::UserLeafTerms(writable, !original.has(cinux::arch::page::kNoExec));
}

}  // namespace

bool MarkPageShared(unsigned long root, unsigned long virtual_address) {
    const arch::IrqGuard    kIrq;
    const auto              kGate = g_fault_gate.guard();
    cinux::mm::KernelTables world;
    const SlotAt            kSlot = FindSlot(world, root, virtual_address);
    if (kSlot.entry == nullptr || !walk::IsLeafLevel(kSlot.level)) {
        return false;
    }
    const Entry kLeaf = *kSlot.entry;
    if (!kLeaf.has(cinux::arch::page::kPresent)) {
        return false;
    }
    if (kLeaf.has(cinux::arch::page::kCow) && !kLeaf.has(cinux::arch::page::kWritable)) {
        return true;
    }
    const unsigned long long kTarget = walk::EntryTarget(kLeaf);
    Pmm::self().retain_frame(cinux::base::PhysAddr{kTarget});
    *kSlot.entry = cinux::arch::page::MakeTableEntry(kTarget, shared_flags(kLeaf));
    cinux::arch::space::InvalidatePage(virtual_address);
    return true;
}

bool MarkChildShared(unsigned long root, unsigned long virtual_address,
                     bool /*parent_was_shared*/) {
    // On this port both sides of the clone count exactly alike: every
    // mark retains once, so the child hook is the parent hook.
    return MarkPageShared(root, virtual_address);
}

bool TryResolvePageFault(unsigned long long address, unsigned long long error_code) {
    const arch::IrqGuard kIrq;
    const auto           kGate = g_fault_gate.guard();
    if ((error_code & cinux::arch::isr::kFaultWasWrite) == 0) {
        return false;
    }
    cinux::mm::KernelTables world;
    const SlotAt            kSlot = FindSlot(world, cinux::arch::space::ActiveRoot(), address);
    if (kSlot.entry == nullptr || !walk::IsLeafLevel(kSlot.level)) {
        return false;
    }
    const Entry kLeaf = *kSlot.entry;
    if (!kLeaf.has(cinux::arch::page::kPresent) || kLeaf.has(cinux::arch::page::kWritable) ||
        !kLeaf.has(cinux::arch::page::kCow)) {
        return false;
    }
    const unsigned long long kTarget  = walk::EntryTarget(kLeaf);
    const unsigned long long kCompany = release_share(kTarget);
    if (kCompany == 0) {
        *kSlot.entry = cinux::arch::page::MakeTableEntry(kTarget, private_flags(kLeaf, true));
        cinux::arch::space::InvalidatePage(address);
        return true;
    }
    const cinux::base::PhysAddr kCopy = cinux::mm::Pmm::self().allocate_page();
    cinux::base::safety::Check(kCopy != cinux::base::PhysAddr{}, "cow copy out of frames");
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* const       kDestination = reinterpret_cast<void*>(cinux::mm::DirectMapVirt(kCopy.raw));
    // NOLINTNEXTLINE(performance-no-intto-ptr)
    const auto* const kSource = reinterpret_cast<const void*>(cinux::mm::DirectMapVirt(kTarget));
    cinux::base::CopyBytes(kDestination, kSource, cinux::arch::page::kSize);
    *kSlot.entry = cinux::arch::page::MakeTableEntry(kCopy.raw, private_flags(kLeaf, true));
    cinux::arch::space::InvalidatePage(address);
    return true;
}

void InstallResolver() {
    cinux::arch::isr::InstallPageFaultResolver(&TryResolvePageFault);
}

void ReleaseFrame(unsigned long long frame) {
    const arch::IrqGuard kIrq;
    const auto           kGate = g_fault_gate.guard();
    if (cinux::mm::IsHardwarePage(static_cast<unsigned long>(frame))) {
        return;
    }
    if (release_share(frame) == 0) {
        cinux::mm::Pmm::self().free_page(cinux::base::PhysAddr{frame});
    }
}

}  // namespace cinux::mm::cow
