/**
 * @file    shared_map.hpp
 * @brief   Mapping one frame into more than one address space.
 *
 * A plain map_user assumes the frame has one owner. Handing the same
 * frame to a second space must first tell the ledger, or the first
 * teardown would free a frame the other space still translates. This
 * helper is the explicit way to say "shared": retain, map, and undo the
 * retain if the map itself fails.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/arch/armv7/cow_ledger.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/cow.hpp"

namespace cinux::armv7 {

/**
 * @brief         Maps a frame into a space read-only and records one owner.
 * @param[in,out] space    The space receiving the mapping.
 * @param[in]     virtual_address   Where the frame appears.
 * @param[in]     physical          The shared frame.
 * @return        true when the ledger and the walk both accepted it.
 * @note          Both sides are mapped read-only; the first write takes a
 *                copy-on-write fault and gets a private writable frame.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline bool MapSharedFrame(cinux::mm::AddressSpace& space, unsigned long virtual_address,
                           unsigned long physical) {
    cinux::mm::cow::RetainFrame(physical);
    if (!space.map_user(virtual_address, physical,
                        cinux::arch::page::kUser | cinux::arch::page::kReadOnly)) {
        cinux::mm::cow::ReleaseFrame(physical);
        return false;
    }
    return true;
}

}  // namespace cinux::armv7
