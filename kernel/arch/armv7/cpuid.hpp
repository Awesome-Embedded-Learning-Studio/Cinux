/**
 * @file    cpuid.hpp
 * @brief   The named facts the processor answers about itself.
 *
 * ARMv7 has no CPUID instruction; the identity lives in the MIDR system
 * register (implementer, variant, part number, revision) and the
 * affinity in MPIDR. This port publishes the same CpuIdentity shape the
 * x86-64 port does, so the procfs side copies fields and never learns
 * which register answered. Header inline on purpose: the question
 * carries no state and each world asks it for free.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "cinux/bit_ops/bitmask.hpp"
#include "cinux/memory.hpp"
#include "cinux/text.hpp"
#include "kernel/arch/smp_control.hpp"

namespace cinux::arch {

/**
 * @brief   One processor's identity under the port-neutral vocabulary.
 * @note    Same shape and meaning as the x86-64 port's CpuIdentity;
 *          fields this architecture cannot answer stay zeroed rather
 *          than guessed.
 * @since   0.1.0
 * @ingroup kernel_arch
 */
struct CpuIdentity {
    /// The affinity identity (MPIDR's Aff0 here).
    unsigned int hardware_id = 0;
    /// MIDR implementer octet (0x41 is ARM Ltd., for one).
    unsigned int family = 0;
    /// MIDR part number (0xC07 is the Cortex-A7, for one).
    unsigned int model = 0;
    /// MIDR revision.
    unsigned int stepping = 0;
    /// Terminated implementer name; empty when unknown.
    char vendor[13]{};
    /// No brand-string register exists on this port; stays empty.
    char model_name[49]{};
    /// No feature bitmap lives in MIDR; stays empty rather than guessed.
    char flags[96]{};
};

namespace midr {

/// MIDR bits [31:24]: the implementer octet.
constexpr base::bit::BitRange kImplementer{.low = 24, .width = 8};
/// MIDR bits [15:4]: the part number.
constexpr base::bit::BitRange kPartNumber{.low = 4, .width = 12};
/// MIDR bits [3:0]: the revision of the part.
constexpr base::bit::BitRange kRevision{.low = 0, .width = 4};

}  // namespace midr

/**
 * @brief         Decodes one online processor's identity from MIDR.
 * @param[in]     ordinal   Zero-based index into the online set.
 * @return        The decoded identity; all zeroes beyond the online set.
 * @note          The single-core board answers ordinal 0 only, which
 *                OnlineCpuCount already states.
 * @since         0.1.0
 * @ingroup kernel_arch
 */
inline CpuIdentity ReadCpuIdentity(unsigned long ordinal) {
    CpuIdentity into{};
    if (ordinal >= OnlineCpuCount()) {
        return into;
    }
    unsigned long midr_value  = 0;
    unsigned long mpidr_value = 0;
    asm volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(midr_value));    // MIDR
    asm volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr_value));  // MPIDR
    using Word        = base::bit::BitMask<unsigned long>;
    const Word kMidr  {midr_value};
    const Word kMpidr {mpidr_value};
    into.family       = kMidr.extract(midr::kImplementer);
    into.model        = kMidr.extract(midr::kPartNumber);
    into.stepping     = kMidr.extract(midr::kRevision);
    into.hardware_id  = kMpidr.extract(base::bit::BitRange{.low = 0, .width = 8});
    struct Implementer {
        unsigned int code;
        const char*  name;
    };
    constexpr Implementer kKnown[] = {
        {.code = 0x41, .name = "ARM"},   {.code = 0x43, .name = "Calxeda"},
        {.code = 0x44, .name = "DEC"},   {.code = 0x46, .name = "Fujitsu"},
        {.code = 0x4D, .name = "Motorola"}, {.code = 0x50, .name = "APM"},
        {.code = 0x51, .name = "Qualcomm"}, {.code = 0x53, .name = "Samsung"},
        {.code = 0x56, .name = "Marvell"},  {.code = 0x69, .name = "Intel"},
    };
    for (const auto& maker : kKnown) {
        if (maker.code == into.family) {
            base::CopyText(into.vendor, maker.name);
            break;
        }
    }
    return into;
}

}  // namespace cinux::arch
