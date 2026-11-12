/**
 * @file    cow_release.cpp
 * @brief   Frame release for the armv7 lane: exclusive frames go back whole.
 *
 * This lane has no copy-on-write yet, so every user frame has exactly one
 * owner: returning it to the frame allocator is the whole job. When COW
 * arrives it will need a ledger here, and this file is where it goes.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.2
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/mm/pmm.hpp"

namespace cinux::mm::cow {

void ReleaseFrame(unsigned long long frame) {
    Pmm::self().free_page(cinux::base::PhysAddr{static_cast<unsigned long>(frame)});
}

}  // namespace cinux::mm::cow
