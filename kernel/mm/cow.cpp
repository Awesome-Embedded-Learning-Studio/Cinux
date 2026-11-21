#include "kernel/mm/cow.hpp"

#include "cinux/addr.hpp"
#include "cinux/assert.hpp"
#include "cinux/ptr.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/arch/page_entry.hpp"
#include "kernel/arch/space_control.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/mm/pmm.hpp"

namespace cinux::mm::cow {

namespace {

using cinux::arch::page::Entry;
constexpr auto kUserWritable = cinux::arch::page::kWritable;  // NOLINT(readability-identifier-naming)
using walk::FindSlot;
using walk::EntriesAt;

struct CloneWalk {
    AddressSpace*   child;
    unsigned long   parent_root;
    const VmaStore* ranges;
};

void share_leaf(Entry& slot, unsigned long spot, const CloneWalk& context) {
    const unsigned long long kTarget = walk::EntryTarget(slot);
    // Read the leaf before any marking: a leaf that already lost its
    // write permission is a shared page joining one more address space,
    // which is how the child-side hook counts its retain.
    const bool kParentWasShared = !cinux::arch::page::LeafTermsOf(slot).has(kUserWritable);
    VmaRegion  region{};
    const bool kCovered = context.ranges != nullptr && context.ranges->lookup(spot, region);
    const VmaRegion* const kRegion = kCovered ? &region : nullptr;
    if (kRegion != nullptr && kRegion->kind == VmaKind::kDevice) {
        return;
    }
    if (kRegion != nullptr && kRegion->shared.holds()) {
        Pmm::self().retain_frame(base::PhysAddr{static_cast<unsigned long>(kTarget)});
        return;
    }
    cinux::base::safety::Check(MarkPageShared(context.parent_root, spot),
                               "fork clone lost a parent page");
    Entry terms = cinux::arch::page::LeafTermsOf(slot);
    cinux::base::safety::Check(context.child->map_user(spot, kTarget, terms),
                               "fork clone refused a map");
    cinux::base::safety::Check(MarkChildShared(context.child->root(), spot, kParentWasShared),
                               "fork clone lost a child page");
}

[[nodiscard]] walk::WalkLevel level_below(walk::WalkLevel level) {
    return static_cast<walk::WalkLevel>(static_cast<unsigned char>(level) + 1);
}

void descend(Entry* table, walk::WalkLevel level, unsigned long base, const CloneWalk& context);

void share_entry(Entry& slot, walk::WalkLevel level, unsigned long spot, const CloneWalk& context) {
    if (walk::IsLeafLevel(level)) {
        share_leaf(slot, spot, context);
        return;
    }
    cinux::base::safety::Check(cinux::arch::page::IsTableEntry(slot, level),
                               "user half holds a large page");
    descend(cinux::base::PtrAt<Entry>(cinux::mm::DirectMapVirt(walk::TableTarget(slot))),
            level_below(level), spot, context);
}

void descend(Entry* table, walk::WalkLevel level, unsigned long base, const CloneWalk& context) {
    const unsigned char kShift = walk::IndexRange(level).low;
    for (unsigned long index = 0; index < EntriesAt(level); ++index) {
        Entry& slot = table[index];
        if (cinux::arch::page::IsPresent(slot)) {
            share_entry(slot, level, base | (index << kShift), context);
        }
    }
}

}  // namespace

bool CloneInto(const AddressSpace& parent, AddressSpace& child, const VmaStore* ranges) {
    const unsigned long kParentRoot = parent.root();
    if (kParentRoot == 0) {
        return false;
    }
    // NOLINTNEXTLINE(misc-const-correctness) the walk rewrites leaf entries
    auto* const     kRoot = cinux::base::PtrAt<Entry>(cinux::mm::DirectMapVirt(kParentRoot));
    const CloneWalk kContext{.child = &child, .parent_root = kParentRoot, .ranges = ranges};
    const walk::WalkLevel kRootLevel = walk::kUpperLevels[0];
    for (unsigned long slot = 0; slot < EntriesAt(kRootLevel); ++slot) {
        if (!cinux::arch::space::IsUserSlot(slot)) {
            continue;
        }
        Entry& leaf = kRoot[slot];
        if (!cinux::arch::page::IsPresent(leaf)) {
            continue;
        }
        descend(cinux::base::PtrAt<Entry>(cinux::mm::DirectMapVirt(walk::TableTarget(leaf))),
                level_below(kRootLevel), slot << walk::IndexRange(kRootLevel).low, kContext);
    }
    return true;
}

AddressSpace* CloneSharedSpace(const AddressSpace& parent, const VmaStore* ranges) {
    auto* const kChild = new AddressSpace{};
    if (kChild == nullptr || !kChild->init()) {
        delete kChild;
        return nullptr;
    }
    if (!CloneInto(parent, *kChild, ranges)) {
        kChild->teardown();
        delete kChild;
        return nullptr;
    }
    if (arch::space::ActiveRoot() == parent.root()) {
        arch::space::SwitchRoot(parent.root());
    }
    return kChild;
}


/// One frame, one more walker.
///
/// The count lives in the allocator beside the frame's own bitmap bit,
/// so a process with a thousand shared pages is no different from one
/// with two; an arch-local table would only be a second place to forget
/// a share. A frame that was private until now enters the count as the
/// owner it already had, then the new mapping joins it: two walkers,
/// not one. Without the owner's own share the first teardown would free
/// a frame the other world still walks.
void RetainFrame(unsigned long long frame) {
    auto& allocator = Pmm::self();
    if (allocator.frame_shares(base::PhysAddr{static_cast<unsigned long>(frame)}) == 0) {
        allocator.retain_frame(base::PhysAddr{static_cast<unsigned long>(frame)});
    }
    allocator.retain_frame(base::PhysAddr{static_cast<unsigned long>(frame)});
}

/// How many spaces still translate this frame.
unsigned long FrameShares(unsigned long long frame) {
    return Pmm::self().frame_shares(base::PhysAddr{static_cast<unsigned long>(frame)});
}

}  // namespace cinux::mm::cow
