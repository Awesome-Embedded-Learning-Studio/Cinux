/**
 * @file    syscall_abi.hpp
 * @brief   The armv7 user/kernel crossing: r7 carries the number.
 *
 * The x86 face speaks "syscall"; this port's crossing is the supervisor
 * call, with the number in r7 and the arguments packed from r0. The
 * neutral callers get the same three, four and six word contracts.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::arch::abi {

/// The sigreturn number this port's signal trampoline issues in r7.
inline constexpr unsigned long kSigreturnNumber = 173;

/// ARM restorer stubs: rt_sigreturn at +0, legacy sigreturn at +8.
/// Each loads r7 then executes svc; bx lr interworks from Thumb handlers.
inline constexpr unsigned char kSignalReturnStub[] = {0xAD, 0x70, 0xA0, 0xE3,
                                                      0x00, 0x00, 0x00, 0xEF,
                                                      // Legacy sigreturn (119), no siginfo prefix.
                                                      0x77, 0x70, 0xA0, 0xE3,
                                                      0x00, 0x00, 0x00, 0xEF};


inline long long CallKernel(unsigned long long number, unsigned long long arg1,
                            unsigned long long arg2, unsigned long long arg3) {
    // NOLINTNEXTLINE(misc-const-correctness)
    long long ret = 0;
    register unsigned long r0 __asm__("r0") = static_cast<unsigned long>(arg1);
    register unsigned long r1 __asm__("r1") = static_cast<unsigned long>(arg2);
    register unsigned long r2 __asm__("r2") = static_cast<unsigned long>(arg3);
    register unsigned long r7 __asm__("r7") = static_cast<unsigned long>(number);
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r7)
                     : "memory");
    ret = static_cast<long long>(static_cast<long>(r0));
    return ret;
}

inline long long CallKernel4(unsigned long long number, unsigned long long arg1,
                             unsigned long long arg2, unsigned long long arg3,
                             unsigned long long arg4) {
    // NOLINTNEXTLINE(misc-const-correctness)
    long long ret = 0;
    register unsigned long r0 __asm__("r0") = static_cast<unsigned long>(arg1);
    register unsigned long r1 __asm__("r1") = static_cast<unsigned long>(arg2);
    register unsigned long r2 __asm__("r2") = static_cast<unsigned long>(arg3);
    register unsigned long r3 __asm__("r3") = static_cast<unsigned long>(arg4);
    register unsigned long r7 __asm__("r7") = static_cast<unsigned long>(number);
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r7)
                     : "memory");
    ret = static_cast<long long>(static_cast<long>(r0));
    return ret;
}

// NOLINTNEXTLINE(readability-function-size) the ABI face itself is seven words wide
inline long long CallKernel6(unsigned long long number, unsigned long long arg1,
                             unsigned long long arg2, unsigned long long arg3,
                             unsigned long long arg4, unsigned long long arg5,
                             unsigned long long arg6) {
    // NOLINTNEXTLINE(misc-const-correctness)
    long long ret = 0;
    register unsigned long r0 __asm__("r0") = static_cast<unsigned long>(arg1);
    register unsigned long r1 __asm__("r1") = static_cast<unsigned long>(arg2);
    register unsigned long r2 __asm__("r2") = static_cast<unsigned long>(arg3);
    register unsigned long r3 __asm__("r3") = static_cast<unsigned long>(arg4);
    register unsigned long r4 __asm__("r4") = static_cast<unsigned long>(arg5);
    register unsigned long r5 __asm__("r5") = static_cast<unsigned long>(arg6);
    register unsigned long r7 __asm__("r7") = static_cast<unsigned long>(number);
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7)
                     : "memory");
    ret = static_cast<long long>(static_cast<long>(r0));
    return ret;
}

}  // namespace cinux::arch::abi
