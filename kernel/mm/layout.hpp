/**
 * @file    layout.hpp
 * @brief   The memory subsystem's published address-space policy.
 *
 * Two kinds of constant live below, and the split is the point of this
 * file. The kernel-half windows and their fixed-offset translations are
 * architecture facts, owned by kernel/arch/space.hpp and republished here
 * so consumers of the memory subsystem name one namespace. The user-half
 * conventions (null trap, interpreter seat, stack span, brk/mmap split)
 * are policy: they say what this kernel promises a user program, not what
 * the CPU forces. A port swaps the arch header; the policy survives.
 *
 * The static_asserts at the bottom keep the two layers ordered against
 * each other, so a port that moves its user top cannot silently invert
 * the stack and the mmap area.
 *
 * @author  Charliechen114514
 * @date    2026-10-05
 * @version 0.2
 * @since   0.1.0
 * @ingroup kernel_mm
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/literal_types.hpp"
#include "kernel/arch/page.hpp"
#include "kernel/arch/space.hpp"

namespace cinux::mm {

using cinux::base::operator""_KiB;
using cinux::base::operator""_MiB;

/** @brief Highest user-visible address plus one. */
using cinux::arch::space::kUserTop;

/** @brief The fixed-offset translations the kernel half provides. */
using cinux::arch::space::DirectMapPhys;
using cinux::arch::space::DirectMapVirt;
using cinux::arch::space::IoremapVirt;
using cinux::arch::space::IsDirectMap;
using cinux::arch::space::IsKernelHalf;

/** @brief Null-trap guard: user mappings never start below this address. */
using cinux::arch::space::kUserNullTop;
/** @brief Fixed load base for the dynamic interpreter a PT_INTERP names. */
using cinux::arch::space::kUserInterpreterBase;
/** @brief Floor for a position-independent main image's load bias. */
using cinux::arch::space::kUserExecImageBase;
/** @brief Boundary between the program-plus-brk area and the mmap area. */
using cinux::arch::space::kUserBrkMmapSplit;
/** @brief Default top of the mmap area; allocations grow down from here. */
using cinux::arch::space::kUserMmapTop;
/** @brief Upper edge of the default main stack; the page above stays unmapped. */
using cinux::arch::space::kUserStackTop;
/** @brief How far the main stack may grow down from its top. */
using cinux::arch::space::kUserStackBytes;
/** @brief Lower edge of the stack's registered range, guard page included. */
using cinux::arch::space::kUserStackBottom;

static_assert(kUserStackTop + cinux::arch::page::kSize == kUserTop,
              "one guard page separates the stack top from user top");
static_assert(kUserMmapTop < kUserStackBottom,
              "the stack's full span stays clear of the mmap area");
static_assert(kUserNullTop < kUserBrkMmapSplit && kUserBrkMmapSplit < kUserMmapTop &&
                  kUserMmapTop < kUserStackTop,
              "user conventions stay ordered inside the user half");

}  // namespace cinux::mm
