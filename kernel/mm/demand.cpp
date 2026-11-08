/**
 * @file    demand.cpp
 * @brief   One fault, one promise, one frame: the fill itself.
 *
 * @author  Charliechen114514
 * @date    2026-10-15
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_mm
 * @copyright Copyright (c) 2026
 */

#include "kernel/mm/demand.hpp"

#include "cinux/addr.hpp"
#include "cinux/math.hpp"
#include "cinux/memory.hpp"
#include "cinux/ptr.hpp"
#include "cinux/result.hpp"
#include "kernel/arch/x86_64/isr.hpp"  // NOLINT(misc-include-cleaner) fault-code words shared with the reporter
#include "kernel/arch/x86_64/page.hpp"
#include "kernel/arch/x86_64/page_entry.hpp"
#include "kernel/arch/x86_64/registers.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/mm/vmm.hpp"

namespace cinux::mm::demand {

namespace {

using arch::page::kSize;
using arch::page::Entry;

/// @brief Reads one page of a file-backed range into a fresh frame's
///        direct-map alias; a short read at the file's tail is the bss
///        answer, so only a refusal fails.
[[nodiscard]] bool read_backing_page(const VmaRegion& region, unsigned long page,
                                     const base::PhysAddr& frame) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* const              kSink = reinterpret_cast<void*>(DirectMapVirt(frame.raw));
    const unsigned long long kFileAt =
        region.file_offset + static_cast<unsigned long long>(page - region.start);
    return region.file_ops->read(region.file_self, region.backing, kFileAt, kSink, kSize).ok();
}

/// @brief Maps one page of a device-backed range straight to the hardware
///        frame the device names; no RAM frame stands between the fault
///        and the hardware.
[[nodiscard]] bool map_device_page(const AddressSpace& space, const VmaRegion& region,
                                   unsigned long page) {
    if (region.file_ops == nullptr) {
        return false;
    }
    const unsigned long long kInto =
        region.file_offset + static_cast<unsigned long long>(page - region.start);
    const base::Result<unsigned long> kFrame =
        region.file_ops->frame_at(region.file_self, region.backing, kInto);
    if (!kFrame.ok()) {
        return false;
    }
    const Entry kBits = arch::page::UserLeafTerms(region.writable, region.executable);
    if (!space.map_user(page, kFrame.value(), kBits)) {
        return false;
    }
    arch::InvalidatePage(page);
    return true;
}

}  // namespace

FillOutcome FillPage(const AddressSpace& space, const VmaStore& ranges, unsigned long long address,
                     unsigned long long error_code) {
    if ((error_code & (arch::isr::kFaultProtection | arch::isr::kFaultReserved)) != 0 ||
        address >= kUserTop) {
        return FillOutcome::kUnhandled;
    }
    // The hold keeps the range alive across the fill: without it a
    // concurrent munmap could free the region between the lookup and
    // the field reads below — the use-after-free this station closes.
    const VmaStore::Hold   kHold(ranges);
    const VmaRegion* const kRegion = ranges.find_locked(address);
    if (kRegion == nullptr) {
        return FillOutcome::kUnhandled;
    }
    const bool kWrite       = (error_code & arch::isr::kFaultWasWrite) != 0;
    const bool kInstruction = (error_code & arch::isr::kFaultInstruction) != 0;
    if ((kWrite && !kRegion->writable) || (kInstruction && !kRegion->executable) ||
        (!kWrite && !kInstruction && !kRegion->readable)) {
        return FillOutcome::kUnhandled;
    }
    const unsigned long kPage = cinux::base::math::AlignDown(address, kSize);
    if (kRegion->kind == VmaKind::kStack && kPage < kRegion->stack_floor) {
        return FillOutcome::kUnhandled;
    }
    KernelTables       world;
    const walk::SlotAt kSlot = walk::FindSlot(world, space.root(), kPage);
    if (kSlot.entry != nullptr && kSlot.entry->has(cinux::arch::page::kPresent)) {
        return FillOutcome::kRetry;
    }
    if (kRegion->shared.holds()) {
        const auto& pages  = kRegion->shared->pages;
        const auto  kIndex = (kPage - kRegion->shared->start) / kSize;
        if (kIndex >= pages->count) {
            return FillOutcome::kUnhandled;
        }
        const auto kPhysical = pages->frames[kIndex];
        if (!space.map_user(kPage, kPhysical,
                            arch::page::UserLeafTerms(kRegion->writable, false))) {
            return FillOutcome::kUnhandled;
        }
        Pmm::self().retain_frame(base::PhysAddr{kPhysical});
        arch::InvalidatePage(kPage);
        return FillOutcome::kFilled;
    }
    if (kRegion->kind == VmaKind::kDevice) {
        return map_device_page(space, *kRegion, kPage) ? FillOutcome::kFilled
                                                       : FillOutcome::kUnhandled;
    }
    const base::PhysAddr kFrame = Pmm::self().allocate_page();
    if (kFrame == base::PhysAddr{}) {
        return FillOutcome::kUnhandled;
    }
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* const kBytes = cinux::base::PtrAt<unsigned char>(DirectMapVirt(kFrame.raw));
    cinux::base::SetBytes(kBytes, 0, kSize);
    if (kRegion->kind == VmaKind::kFile &&
        (kRegion->file_ops == nullptr || !read_backing_page(*kRegion, kPage, kFrame))) {
        Pmm::self().free_page(kFrame);
        return FillOutcome::kUnhandled;
    }
    Entry bits = cinux::arch::page::UserLeafTerms(kRegion->writable, kRegion->executable);
    if (!space.map_user(kPage, kFrame.raw, bits)) {
        Pmm::self().free_page(kFrame);
        return FillOutcome::kUnhandled;
    }
    cinux::arch::InvalidatePage(kPage);
    return FillOutcome::kFilled;
}

}  // namespace cinux::mm::demand
