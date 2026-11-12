#include "kernel/mm/address_space.hpp"

#include "cinux/addr.hpp"
#include "cinux/assert.hpp"
#include "cinux/memory.hpp"
#include "cinux/ptr.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/space_control.hpp"
#include "kernel/mm/cow.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vmm.hpp"

namespace cinux::mm {

namespace {

using cinux::arch::page::Entry;
using walk::MapPage;
using walk::MapUserPage;
using walk::TranslatePage;

constexpr unsigned long kSlotBytes       = sizeof(Entry);

constexpr int kLeafTable       = 0;

constexpr unsigned long kUpperLevelCount =
    sizeof(walk::kUpperLevels) / sizeof(walk::kUpperLevels[0]);

/// Retires a table and everything under it.
/// @param level_index  Index into walk::kUpperLevels; one past the end
///                     means the table holds leaf entries.
void release_table(Entry* table, unsigned long level_index) {
    const auto kLevel = (level_index < kUpperLevelCount)
                            ? walk::kUpperLevels[level_index]
                            : cinux::arch::page::kLeafLevel;
    for (unsigned long index = 0; index < walk::kEntriesPerTable; ++index) {
        Entry& slot = table[index];
        if (!cinux::arch::page::DescriptorPresent(slot)) {
            continue;
        }
        if (cinux::arch::page::DescriptorIsLeaf(slot, kLevel)) {
            cow::ReleaseFrame(walk::EntryTarget(slot));
            continue;
        }
        const unsigned long kChild = walk::EntryTarget(slot);
        auto* const kChildTable = cinux::base::PtrAt<Entry>(DirectMapVirt(kChild));
        if (level_index + 1U >= kUpperLevelCount) {
            for (unsigned long leaf = 0; leaf < walk::kEntriesPerTable; ++leaf) {
                Entry& leaf_slot = kChildTable[leaf];
                if (cinux::arch::page::DescriptorPresent(leaf_slot)) {
                    cow::ReleaseFrame(walk::EntryTarget(leaf_slot));
                }
            }
        } else {
            release_table(kChildTable, level_index + 1U);
        }
        Pmm::self().free_page(cinux::base::PhysAddr{kChild});
    }
}

}  // namespace

bool AddressSpace::init() {
    const arch::IrqGuard kIrq;
    const auto           kGate = gate_.guard();
    KernelTables         tables;
    const unsigned long  kRoot = tables.take_root();
    if (kRoot == 0) {
        return false;
    }
    auto* const        kFresh  = cinux::base::PtrAt<Entry>(DirectMapVirt(kRoot));
    const Entry* const kKernel = cinux::base::PtrAt<Entry>(DirectMapVirt(KernelPageRoot()));
    const unsigned long kTopSlots = cinux::arch::page::EntriesAt(walk::kUpperLevels[0]);
    for (unsigned long index = 0; index < kTopSlots; ++index) {
        kFresh[index] = cinux::arch::space::IsUserSlot(index) ? Entry{} : kKernel[index];
    }
    root_ = kRoot;
    return true;
}

unsigned long AddressSpace::root() const {
    return root_;
}

bool AddressSpace::map(unsigned long virtual_address, unsigned long physical) const {
    const arch::IrqGuard kIrq;
    const auto           kGate = gate_.guard();
    if (root_ == 0) {
        return false;
    }
    KernelTables tables;
    return MapPage(tables, root_, virtual_address, physical);
}

bool AddressSpace::map_user(unsigned long virtual_address, unsigned long physical,
                            cinux::arch::page::Entry leaf_bits) const {
    const arch::IrqGuard kIrq;
    const auto           kGate = gate_.guard();
    if (root_ == 0) {
        return false;
    }
    KernelTables tables;
    return MapUserPage(tables, root_, virtual_address,
                       cinux::arch::page::MakePageEntry(physical, leaf_bits));
}

unsigned long AddressSpace::translate(unsigned long virtual_address) const {
    const arch::IrqGuard kIrq;
    const auto           kGate = gate_.guard();
    if (root_ == 0) {
        return 0;
    }
    KernelTables tables;
    return TranslatePage(tables, root_, virtual_address);
}

void AddressSpace::activate() const {
    if (root_ != 0) {
        cinux::arch::space::SwitchRoot(root_);
    }
}

void AddressSpace::teardown() {
    const arch::IrqGuard kIrq;
    const auto           kGate = gate_.guard();
    if (root_ == 0) {
        return;
    }
    KernelTables tables;
    // NOLINTNEXTLINE(misc-const-correctness) the walk rewrites leaf entries
    auto* const kRoot = cinux::base::PtrAt<Entry>(DirectMapVirt(root_));
    const unsigned long kTopSlots = cinux::arch::page::EntriesAt(walk::kUpperLevels[0]);
    for (unsigned long index = 0; index < kTopSlots; ++index) {
        if (!cinux::arch::space::IsUserSlot(index)) {
            continue;
        }
        Entry& slot = kRoot[index];
        if (!cinux::arch::page::DescriptorPresent(slot)) {
            continue;
        }
        unsigned long const kTarget = walk::EntryTarget(slot);
        release_table(cinux::base::PtrAt<Entry>(DirectMapVirt(kTarget)), 1U);
        Pmm::self().free_page(cinux::base::PhysAddr{kTarget});
    }
    tables.release_root(root_);
    root_ = 0;
}

}  // namespace cinux::mm
