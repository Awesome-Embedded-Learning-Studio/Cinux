/**
 * @file    cpuid.hpp
 * @brief   The named facts the processor answers about itself.
 *
 * CPUID is the processor's self-description service: one instruction,
 * a leaf number in EAX (and a subleaf in ECX for the strided leaves),
 * four words of answer. The kernel asks it exactly one kind of
 * question — "is this CR4 bit implemented?" — and every protection bit
 * from the 2010s answers through leaf 7. Header inline on purpose: the
 * question carries no state and each world asks it for free.
 *
 * @author  Charliechen114514
 * @date    2026-10-23
 * @version 0.3.0
 * @since   0.3.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>
#include <span>

#include "cinux/bit_ops/bitmask.hpp"
#include "cinux/memory.hpp"
#include "cinux/text.hpp"
#include "kernel/arch/smp_control.hpp"

namespace cinux::arch {

/// The four words CPUID fills for one (leaf, subleaf) question.
struct CpuIdWords {
    /// EAX on return; for leaf 0 it names the highest standard leaf.
    unsigned int eax{};

    /// EBX on return; leaf 7 carries the protection-feature bitmap here.
    unsigned int ebx{};

    /// ECX on return.
    unsigned int ecx{};

    /// EDX on return.
    unsigned int edx{};
};

/**
 * @brief         Asks the processor one CPUID question.
 *
 * @param[in]     leaf     The main selector, EAX on entry.
 * @param[in]     subleaf  The strided selector, ECX on entry; leaves
 *                         without strides ignore it and pass zero.
 * @return        The four answer words.
 * @note          Asking a leaf beyond the processor's knowledge is
 *                undefined territory — the named facts below ask leaf 0
 *                first and so should any new question.
 * @warning       None
 * @throws        None
 * @since         0.3.0
 * @ingroup       kernel_arch
 */
inline CpuIdWords ReadCpuId(unsigned int leaf, unsigned int subleaf) {
    CpuIdWords   answer{};
    // NOLINTNEXTLINE(misc-const-correctness)
    unsigned int selected = leaf;
    // NOLINTNEXTLINE(misc-const-correctness)
    unsigned int stride   = subleaf;
    asm volatile("cpuid" : "+a"(selected), "+b"(answer.ebx), "+c"(stride), "+d"(answer.edx));
    answer.eax = selected;
    answer.ecx = stride;
    return answer;
}

/**
 * @brief         Reports whether the processor implements SMEP.
 *
 * @return        True when CPUID leaf 7 names CR4 bit 20 available.
 * @note          Writing the bit into CR4 on a processor without it
 *                fails the whole write with \#GP, so the enable step
 *                asks here first and skips cleanly when the answer is
 *                no.
 * @warning       None
 * @throws        None
 * @since         0.3.0
 * @ingroup       kernel_arch
 */
inline bool HasSmep() {
    if (ReadCpuId(0U, 0U).eax < 7U) {
        return false;
    }
    return (ReadCpuId(7U, 0U).ebx & (1U << 7U)) != 0U;
}

/**
 * @brief         Reports whether the processor implements SMAP.
 *
 * @return        True when CPUID leaf 7 names CR4 bit 21 available.
 * @note          Same discipline as SMEP: ask first, write only on a
 *                yes, and the mechanism printout tells the truth when
 *                the answer is no.
 * @warning       None
 * @throws        None
 * @since         0.3.0
 * @ingroup       kernel_arch
 */
inline bool HasSmap() {
    if (ReadCpuId(0U, 0U).eax < 7U) {
        return false;
    }
    return (ReadCpuId(7U, 0U).ebx & (1U << 20U)) != 0U;
}

/**
 * @brief   One processor's identity under the port-neutral vocabulary.
 * @note    The fields mirror what /proc/cpuinfo prints, so the procfs
 *          side only copies; every port publishes the same shape here
 *          and the decoding stays where the registers live.
 * @since   0.1.0
 * @ingroup kernel_arch
 */
struct CpuIdentity {
    /// The interrupt-controller identity (APIC id here).
    unsigned int hardware_id = 0;
    /// CPU family after the extended-family fold.
    unsigned int family = 0;
    /// CPU model after the extended-model fold.
    unsigned int model = 0;
    /// Processor revision (stepping).
    unsigned int stepping = 0;
    /// Terminated CPUID vendor identifier.
    char vendor[13]{};
    /// Terminated CPUID brand string; empty when the leaf is absent.
    char model_name[49]{};
    /// Space-separated feature names; empty when leaf 1 is absent.
    char flags[96]{};
};

/**
 * @brief         Decodes one online processor's identity from CPUID.
 * @param[in]     ordinal   Zero-based index into the online set.
 * @return        The decoded identity; fields stay zeroed when the
 *                processor answers less than the question asks.
 * @note          The caller bounds the ordinal against OnlineCpuCount;
 *                beyond the online set the answer is all zeroes.
 * @since         0.1.0
 * @ingroup       arch_bootstrap
 */
inline CpuIdentity ReadCpuIdentity(unsigned long ordinal) {
    CpuIdentity into{};
    if (ordinal >= OnlineCpuCount()) {
        return into;
    }
    into.hardware_id = CpuHardwareId(static_cast<unsigned int>(ordinal));
    const auto kVendor = ReadCpuId(0, 0);
    base::CopyBytes(into.vendor, &kVendor.ebx, sizeof(kVendor.ebx));
    base::CopyBytes(into.vendor + sizeof(kVendor.ebx), &kVendor.edx, sizeof(kVendor.edx));
    base::CopyBytes(into.vendor + sizeof(kVendor.ebx) + sizeof(kVendor.edx), &kVendor.ecx,
                    sizeof(kVendor.ecx));
    if (kVendor.eax >= 1) {
        const auto kIdentity = ReadCpuId(1, 0);
        using Word           = base::bit::BitMask<std::uint32_t>;
        constexpr base::bit::BitRange kStepping{.low = 0, .width = 4};
        constexpr base::bit::BitRange kBaseModel{.low = 4, .width = 4};
        constexpr base::bit::BitRange kBaseFamily{.low = 8, .width = 4};
        constexpr base::bit::BitRange kExtendedModel{.low = 16, .width = 4};
        constexpr base::bit::BitRange kExtendedFamily{.low = 20, .width = 8};
        const Word                    kSignature{kIdentity.eax};
        const auto                    kFamily = kSignature.extract(kBaseFamily);
        into.family = kFamily == 15 ? kFamily + kSignature.extract(kExtendedFamily) : kFamily;
        Word model{kSignature.extract(kBaseModel)};
        if (kFamily == 6 || kFamily == 15) {
            model.deposit(kBaseModel, kSignature.extract(kExtendedModel));
        }
        into.model    = model.raw;
        into.stepping = kSignature.extract(kStepping);
        struct CpuFlag {
            unsigned char position;
            const char*   name;
        };
        constexpr CpuFlag kFlags[] = {
            {.position = 0, .name = "fpu"},   {.position = 4, .name = "tsc"},
            {.position = 9, .name = "apic"},  {.position = 12, .name = "mtrr"},
            {.position = 15, .name = "cmov"}, {.position = 22, .name = "acpi"},
            {.position = 23, .name = "mmx"},  {.position = 24, .name = "fxsr"},
            {.position = 25, .name = "sse"},  {.position = 26, .name = "sse2"},
        };
        const Word    kFeatures{kIdentity.edx};
        unsigned long used = 0;
        for (const auto& flag : kFlags) {
            if (kFeatures.has(base::bit::MaskBit<std::uint32_t>(flag.position))) {
                if (used != 0) {
                    into.flags[used++] = ' ';
                }
                used += base::CopyText(std::span{into.flags}.subspan(used), flag.name);
            }
        }
    }
    if (ReadCpuId(0x80000000U, 0).eax >= 0x80000004U) {
        for (unsigned int leaf = 0; leaf < 3; ++leaf) {
            const auto         kBrand   = ReadCpuId(0x80000002U + leaf, 0);
            const unsigned int kWords[] = {kBrand.eax, kBrand.ebx, kBrand.ecx, kBrand.edx};
            base::CopyBytes(into.model_name + (leaf * sizeof(kWords)), kWords, sizeof(kWords));
        }
    }
    return into;
}

}  // namespace cinux::arch
