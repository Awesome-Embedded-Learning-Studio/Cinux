/**
 * @file    isr.hpp
 * @brief   The armv7 exception vocabulary: the abort vector, the fault
 *          code the rest of the kernel reads, and the frame it came on.
 *
 * The CPU's own report is DFSR: a status code in its low bits and a
 * write-not-read bit at 11. The kernel's consumers expect the same
 * synthetic word x86 hands out (permission, write, user), so
 * FaultCodeFromStatus folds DFSR and SPSR into that word once, here.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::arch::isr {

/// The one door a memory world installs to hear page faults: the same
/// name and signature the neutral resolver is written against. On this
/// port the abort handler asks it first and the seat retires the
/// episode when the answer is no.
using PageFaultResolver = bool (*)(unsigned long long address, unsigned long long error_code);
void InstallPageFaultResolver(PageFaultResolver resolver);

/// The vector slot a data abort occupies in the table.
inline constexpr unsigned long long kPageFaultVector = 4;

/// Synthetic fault code, bit 0: permission, not translation.
inline constexpr unsigned long long kFaultProtection = 1ULL << 0U;

/// Synthetic fault code, bit 1: the access was a write.
inline constexpr unsigned long long kFaultWasWrite = 1ULL << 1U;

/// Synthetic fault code, bit 2: the access came from user mode.
inline constexpr unsigned long long kFaultFromUser = 1ULL << 2U;

inline constexpr unsigned long long kFaultReserved    = 1ULL << 3U;
inline constexpr unsigned long long kFaultInstruction = 1ULL << 4U;

/// The registers an abort leaves behind, in push order.
struct [[gnu::packed]] InterruptFrame {
    unsigned long r0;
    unsigned long r1;
    unsigned long r2;
    unsigned long r3;
    unsigned long r4;
    unsigned long r5;
    unsigned long r6;
    unsigned long r7;
    unsigned long r8;
    unsigned long r9;
    unsigned long r10;
    unsigned long r11;
    unsigned long r12;
    unsigned long sp;
    unsigned long lr;
    unsigned long pc;
    unsigned long spsr;
};

/// DFSR bit 11: the access was a write.
inline constexpr unsigned long kDfsrWrite = 1UL << 11U;

/**
 * @brief         Folds DFSR and SPSR into the kernel's synthetic code.
 * @param[in]     dfsr   Data Fault Status Register.
 * @param[in]     spsr   Saved program status of the faulting mode.
 * @return        bit0 permission, bit1 write, bit2 user, as consumers know them.
 * @note          Permission statuses are 0b01101 and 0b01111; translation
 *                and alignment fold to "not permission".
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
constexpr unsigned long long FaultCodeFromStatus(unsigned long dfsr, unsigned long spsr) {
    unsigned long long code = 0;
    // Short-descriptor fault status: bits [3:0] plus bit [10] as the
    // status's fifth bit, so the permission pair stays recognizable.
    const unsigned long status = (dfsr & 0xFUL) | ((dfsr >> 6) & 0x10UL);
    if (status == 0x0DUL || status == 0x0FUL) {
        code |= kFaultProtection;
    }
    if ((dfsr & kDfsrWrite) != 0) {
        code |= kFaultWasWrite;
    }
    if ((spsr & 0x1FUL) == 0x10UL) {  // user mode
        code |= kFaultFromUser;
    }
    return code;
}

}  // namespace cinux::arch::isr
