/**
 * @file    cow_clone.cpp
 * @brief    The armv7 side of the neutral clone-space contract.
 *
 * mm::cow::CloneSharedSpace is the one door the neutral fork path
 * knocks on. On this port the walk is the short-descriptor clone in
 * clone_space.hpp: every present user leaf is retained into a fresh
 * second-level table and both sides drop to user read-only, so the
 * first write on either side takes the copy-on-write repair. The
 * range register rides along separately: BuildForkChild clones the
 * VmaStore itself, and the armv7 walk needs no file-backed probing.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/clone_space.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/cow.hpp"
#include "kernel/mm/vma.hpp"

namespace cinux::mm::cow {

cinux::mm::AddressSpace* CloneSharedSpace(const cinux::mm::AddressSpace& parent,
                                          const cinux::mm::VmaStore* /*ranges*/) {
    auto* const child = new cinux::mm::AddressSpace{};
    if (child == nullptr) {
        return nullptr;
    }
    if (!child->init() ||
        !cinux::armv7::CloneUserSpace(const_cast<cinux::mm::AddressSpace&>(parent), *child)) {
        child->teardown();
        delete child;
        return nullptr;
    }
    return child;
}

}  // namespace cinux::mm::cow
