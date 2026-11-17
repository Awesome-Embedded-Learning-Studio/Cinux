/**
 * @file    syscall_abi.hpp
 * @brief   The x86-64 bridge from ring 3 into the kernel: one SYSCALL
 *          instruction and the register contract around it.
 *
 * The number travels in RAX, the three arguments in RDI, RSI, RDX, and
 * the answer comes back in RAX. RCX and R11 are banked by the instruction
 * itself, so they are listed as clobbered along with memory.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::arch::abi {

/// The sigreturn number the x86 signal trampoline issues in eax.
inline constexpr unsigned long kSigreturnNumber = 15;

/// x86 machine code: mov eax, 15; syscall.
inline constexpr unsigned char kSignalReturnStub[] = {0xB8, 0x0F, 0x00, 0x00,
                                                      0x00, 0x0F, 0x05};


/**
 * @brief         Issue one syscall; the ABI constant, spelled once.
 * @param[in]     number   Syscall number, lands in RAX.
 * @param[in]     arg1     First argument, lands in RDI.
 * @param[in]     arg2     Second argument, lands in RSI.
 * @param[in]     arg3     Third argument, lands in RDX.
 * @return        Whatever the kernel hands back in RAX.
 * @note          A port swaps this header; the caller-facing wrappers in
 *                user/api/syscall.hpp never name the instruction.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
inline long long CallKernel(unsigned long long number, unsigned long long arg1,
                            unsigned long long arg2, unsigned long long arg3) {
    // NOLINTNEXTLINE(misc-const-correctness)
    long long ret = 0;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
                     : "rcx", "r11", "memory");
    return ret;
}

/**
 * @brief         Issue one syscall with four arguments.
 * @param[in]     number   Syscall number, lands in RAX.
 * @param[in]     arg1     First argument, lands in RDI.
 * @param[in]     arg2     Second argument, lands in RSI.
 * @param[in]     arg3     Third argument, lands in RDX.
 * @param[in]     arg4     Fourth argument, lands in R10.
 * @return        Whatever the kernel hands back in RAX.
 * @note          The fourth argument leaves the plain three-register
 *                door: R10 stands in for RCX, which the instruction
 *                banks itself.
 * @since         0.5.0
 * @ingroup       kernel_arch
 */
// NOLINTNEXTLINE(readability-function-size) the syscall's own shape
inline long long CallKernel4(unsigned long long number, unsigned long long arg1,
                             unsigned long long arg2, unsigned long long arg3,
                             unsigned long long arg4) {
    // NOLINTNEXTLINE(misc-const-correctness)
    long long                         ret                    = 0;
    register const unsigned long long kFourth __asm__("r10") = arg4;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3), "r"(kFourth)
                     : "rcx", "r11", "memory");
    return ret;
}

/**
 * @brief         Issue one syscall with the full six-argument contract.
 * @param[in]     number   Syscall number, lands in RAX.
 * @param[in]     arg1     First argument, lands in RDI.
 * @param[in]     arg2     Second argument, lands in RSI.
 * @param[in]     arg3     Third argument, lands in RDX.
 * @param[in]     arg4     Fourth argument, lands in R10.
 * @param[in]     arg5     Fifth argument, lands in R8.
 * @param[in]     arg6     Sixth argument, lands in R9.
 * @return        Whatever the kernel hands back in RAX.
 * @note          A port replaces this header wholesale; the wrappers in
 *                user/api/syscall.hpp keep their names and arity.
 * @since         0.4.0
 * @ingroup       kernel_arch
 */
// NOLINTNEXTLINE(readability-function-size) the ABI face itself is seven words wide
inline long long CallKernel6(unsigned long long number, unsigned long long arg1,
                             unsigned long long arg2, unsigned long long arg3,
                             unsigned long long arg4, unsigned long long arg5,
                             unsigned long long arg6) {
    // NOLINTNEXTLINE(misc-const-correctness)
    long long                         ret                    = 0;
    register const unsigned long long kFourth __asm__("r10") = arg4;
    register const unsigned long long kFifth __asm__("r8")   = arg5;
    register const unsigned long long kSixth __asm__("r9")   = arg6;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3), "r"(kFourth), "r"(kFifth),
                       "r"(kSixth)
                     : "rcx", "r11", "memory");
    return ret;
}

/**
 * @brief         Issue clone and, in the child, land on its own stack and
 *                call the entry the father asked for.
 * @param[in]     number      Syscall number, lands in RAX.
 * @param[in]     flags       Clone flags.
 * @param[in]     child_stack First stack pointer the child grows down from.
 * @param[in]     tls         Thread pointer for the TLS flag.
 * @param[in]     entry       First function the child runs.
 * @param[in]     arg         Handed to entry in RDI.
 * @param[in]     parent_tid  Where the maker's copy of the tid lands.
 * @param[in]     child_tid   Where the child's copy of the tid lands.
 * @return        The child's tid in the maker; the child never returns here.
 * @note          The child resumes at the same instruction on an empty
 *                stack, where a C epilogue would pop garbage, so the
 *                trampoline lives in this asm: it zeroes RBP, moves entry
 *                and arg out of R9 and R15, aligns the stack, calls, and
 *                traps if the entry ever returns. A port writes its own
 *                trampoline in its own registers.
 * @since         0.8.0
 * @ingroup       kernel_arch
 */
// NOLINTNEXTLINE(readability-function-size) the child path lives here by necessity
inline long long CallKernelClone(unsigned long long number, unsigned long long flags,
                                 const void* child_stack, unsigned long long tls,
                                 void (*entry)(void*), const void* arg,
                                 unsigned long long parent_tid, unsigned long long child_tid) {
    // NOLINTBEGIN(misc-const-correctness) the syscall writes ret in the maker
    long long                         ret      = 0;
    const auto                        kEntryAt = reinterpret_cast<unsigned long long>(entry);
    const auto                        kArgAt   = reinterpret_cast<unsigned long long>(arg);
    register const unsigned long long kChildTid __asm__("r10") = child_tid;
    register const unsigned long long kTls __asm__("r8")       = tls;
    register const unsigned long long kEntry __asm__("r9")     = kEntryAt;
    register const unsigned long long kArg __asm__("r15")      = kArgAt;
    // NOLINTEND(misc-const-correctness)
    __asm__ volatile(
        "syscall\n\t"
        "testl %%eax, %%eax\n\t"
        "jnz 1f\n\t"
        "xorl %%ebp, %%ebp\n\t"
        "movq %%r9, %%rax\n\t"
        "movq %%r15, %%rdi\n\t"
        "andq $-16, %%rsp\n\t"
        "call *%%rax\n\t"
        "ud2\n"
        "1:\n"
        : "=a"(ret)
        : "a"(number), "D"(flags), "S"(child_stack), "d"(parent_tid), "r"(kChildTid), "r"(kTls),
          "r"(kEntry), "r"(kArg)
        : "rcx", "r11", "memory");
    return ret;
}

}  // namespace cinux::arch::abi
