/**
 * @file    cow_repair.cpp
 * @brief    The armv7 write-fault repair: private copies for shared pages.
 *
 * One seam for every armv7 world that shares frames: a write fault
 * against a present mapping either leaves a shared frame for a private
 * copy (the ledger drops one owner, the last one frees) or re-arms a
 * read-only private page. Both branches copy the frame and remap the
 * leaf writable; the only difference is whether the old frame still
 * has other owners. The legacy task world and the seated scheduler
 * world both come through here, so the copy logic exists exactly once.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/cow_repair.hpp"

#include "kernel/arch/armv7/cow_ledger.hpp"
#include "kernel/arch/armv7/user_access.hpp"
#include "kernel/boot/print.hpp"
#include "kernel/arch/page_walk.hpp"
#include "kernel/mm/page_walk.hpp"
#include "kernel/mm/pmm.hpp"
#include "kernel/mm/vmm.hpp"

namespace cinux::mm::cow {

bool RepairWriteFault(unsigned long root, unsigned long address) {
    const unsigned long  kPage = address & ~(cinux::arch::page::kSize - 1UL);
    KernelTables kTables;
    const unsigned long kFrame = walk::TranslatePage(kTables, root, kPage);
    if (kFrame == 0) {
        return false;  // nothing present: the demand pager's business
    }
    const bool kShared = FrameShares(kFrame) >= 2;
    if (!kShared && cinux::arch::UserPageIsWritable(kPage)) {
        return false;  // a writable leaf that still write-faults is not ours
    }

    const auto kCopy = Pmm::self().allocate_page();
    if (kCopy.raw == 0) {
        return false;
    }
    const auto* const kSource = reinterpret_cast<const unsigned char*>(kFrame);
    auto* const       kTarget = reinterpret_cast<unsigned char*>(kCopy.raw);
    for (unsigned long index = 0; index < cinux::arch::page::kSize; ++index) {
        kTarget[index] = kSource[index];
    }
    auto* const kTable = walk::EnsureLeafTable(kTables, root, kPage, cinux::arch::page::kUser);
    if (kTable == nullptr) {
        Pmm::self().free_page(kCopy);
        return false;
    }
    kTable[cinux::arch::page::SlotIndex(kPage, cinux::arch::page::kLeafLevel)] =
        cinux::arch::page::MakePageEntry(kCopy.raw,
                                         cinux::arch::page::kUser | cinux::arch::page::kWritable);
    cinux::arch::space::InvalidatePage(kPage);
    ReleaseFrame(kFrame);
    if (kShared) {
        cinux::print::Println("[armv7] cow copy at %X", kPage);
    } else {
        cinux::print::Println("[armv7] ro copy at %X", kPage);
    }
    return true;
}

}  // namespace cinux::mm::cow
