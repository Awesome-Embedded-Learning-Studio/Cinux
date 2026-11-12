/**
 * @file    cow_release.cpp
 * @brief   Frame release and the share ledger for the armv7 lane.
 *
 * Exclusive frames go straight back to the allocator. Shared frames are
 * counted in a small table: the last owner frees, everyone else just
 * drops a count. No copy happens yet; that is the fault path's job and
 * it does not exist on this port.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.3
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/cow_ledger.hpp"

#include "kernel/mm/pmm.hpp"

namespace cinux::mm::cow {

namespace {

constexpr unsigned long kLedgerEntries = 128;

struct SharedFrame {
    unsigned long long frame;
    unsigned long      count;
};

SharedFrame g_ledger[kLedgerEntries];

SharedFrame* find_shared(unsigned long long frame) {
    for (auto& entry : g_ledger) {
        if (entry.frame == frame) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

void RetainFrame(unsigned long long frame) {
    if (SharedFrame* entry = find_shared(frame); entry != nullptr) {
        ++entry->count;
        return;
    }
    for (auto& entry : g_ledger) {
        if (entry.frame == 0) {
            entry.frame = frame;
            entry.count = 2;  // the owner that asked plus the one before
            return;
        }
    }
}

unsigned long FrameShares(unsigned long long frame) {
    const SharedFrame* entry = find_shared(frame);
    return entry == nullptr ? 0 : entry->count;
}

void ReleaseFrame(unsigned long long frame) {
    if (SharedFrame* entry = find_shared(frame); entry != nullptr) {
        if (entry->count > 1) {
            --entry->count;
            return;
        }
        entry->frame = 0;
        entry->count = 0;
    }
    Pmm::self().free_page(cinux::base::PhysAddr{static_cast<unsigned long>(frame)});
}

}  // namespace cinux::mm::cow
