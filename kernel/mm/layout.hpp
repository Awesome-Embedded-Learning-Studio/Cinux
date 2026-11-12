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

/** @brief Bytes one top-level table entry spans. */
using cinux::arch::space::kPageTableSpan;

/** @brief Top-level slots of the kernel's fixed windows. */
using cinux::arch::space::kDirectMapSlot;
using cinux::arch::space::kHeapSlot;
using cinux::arch::space::kIoremapSlot;
using cinux::arch::space::kKernelSlot;
using cinux::arch::space::kVmallocSlot;

/** @brief Bases of the kernel's fixed windows. */
using cinux::arch::space::kDirectMapBase;
using cinux::arch::space::kHeapBase;
using cinux::arch::space::kIoremapBase;
using cinux::arch::space::kKernelImageBase;
using cinux::arch::space::kModuleBase;
using cinux::arch::space::kVmallocBase;

/** @brief Sub-slots of the kernel image window. */
using cinux::arch::space::kKernelImagePdpt;
using cinux::arch::space::kModulePdpt;

/** @brief Highest user-visible address plus one. */
using cinux::arch::space::kUserTop;

/** @brief The fixed-offset translations the kernel half provides. */
using cinux::arch::space::DirectMapPhys;
using cinux::arch::space::DirectMapVirt;
using cinux::arch::space::IoremapVirt;
using cinux::arch::space::IsDirectMap;
using cinux::arch::space::IsKernelHalf;

/** @brief Null-trap guard: user mappings never start below this address. */
inline constexpr unsigned long kUserNullTop         = 64_KiB;
/** @brief Fixed load base for the dynamic interpreter a PT_INTERP names;
           deterministic on purpose, ASLR is a later station's question. */
inline constexpr unsigned long kUserInterpreterBase = 256_MiB;
/** @brief Floor for a position-independent main image's load bias; the
           per-exec slide stays inside one mebibyte above it, keeping the
           seat clear of the interpreter's base below. */
inline constexpr unsigned long kUserExecImageBase   = 512_MiB;
/** @brief Default boundary between the program-plus-brk area and the mmap area. */
inline constexpr unsigned long kUserBrkMmapSplit    = 0x0000400000000000UL;
/** @brief Default top of the mmap area; allocations grow down from here. */
inline constexpr unsigned long kUserMmapTop         = 0x00007F8000000000UL;
/** @brief Upper edge of the default main stack; the page above stays unmapped. */
inline constexpr unsigned long kUserStackTop        = kUserTop - cinux::arch::page::kSize;
/** @brief How far the main stack may grow down from its top: one megabyte
           of address range, filled page by page on demand. */
inline constexpr unsigned long kUserStackBytes      = 1_MiB;
/** @brief Lower edge of the stack's registered range: the stack span plus
           one guard page below it, which demand filling refuses to map. */
inline constexpr unsigned long kUserStackBottom =
    kUserStackTop - kUserStackBytes - cinux::arch::page::kSize;

static_assert(kUserStackTop + cinux::arch::page::kSize == kUserTop,
              "one guard page separates the stack top from user top");
static_assert(kUserMmapTop < kUserStackBottom,
              "the stack's full span stays clear of the mmap area");
static_assert(kUserNullTop < kUserBrkMmapSplit && kUserBrkMmapSplit < kUserMmapTop &&
                  kUserMmapTop < kUserStackTop,
              "user conventions stay ordered inside the user half");

}  // namespace cinux::mm
