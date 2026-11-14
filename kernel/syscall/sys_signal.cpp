#include "kernel/syscall/sys_signal.hpp"

#include "kernel/arch/abi.hpp"
#include "kernel/arch/irq_guard.hpp"
#include "kernel/mm/vma.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/signals.hpp"
#include "kernel/proc/task.hpp"
#include "kernel/syscall/signal.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/syscall_internal.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::syscall {

namespace {

/// rt_sigprocmask's how words: add to the mask, remove from it, replace it.
constexpr Word kSigBlock   = 0;
constexpr Word kSigUnblock = 1;
constexpr Word kSigSetmask = 2;

/// One action record crosses the boundary in this ABI's shape.
constexpr Word kActionBytes = arch::abi::kSigActionLayout.bytes;

/// Reads one action record out of the caller's memory, field by field,
/// so a 20-byte EABI record and a 32-byte x86-64 record both land in
/// the neutral structure.
bool read_action(Word at, SigAction& action) {
    constexpr arch::abi::SigActionLayout kLayout = arch::abi::kSigActionLayout;
    unsigned char raw[kLayout.bytes] = {};
    if (!uaccess::CopyFromUser(raw, at, sizeof(raw))) {
        return false;
    }
    const auto take = [&raw](unsigned long offset, unsigned long width) {
        unsigned long long value = 0;
        for (unsigned long index = 0; index < width; ++index) {
            value |= static_cast<unsigned long long>(raw[offset + index]) << (index * 8U);
        }
        return value;
    };
    action.handler  = take(kLayout.handler_off, kLayout.handler_size);
    action.flags    = take(kLayout.flags_off, kLayout.flags_size);
    action.restorer = take(kLayout.restorer_off, kLayout.restorer_size);
    action.mask     = SigSet{take(kLayout.mask_off, kLayout.mask_size)};
    return true;
}

/// Writes one action record back in the same shape.
bool write_action(Word at, const SigAction& action) {
    constexpr arch::abi::SigActionLayout kLayout = arch::abi::kSigActionLayout;
    unsigned char raw[kLayout.bytes] = {};
    const auto put = [&raw](unsigned long offset, unsigned long width,
                            unsigned long long value) {
        for (unsigned long index = 0; index < width; ++index) {
            raw[offset + index] = static_cast<unsigned char>((value >> (index * 8U)) & 0xFFU);
        }
    };
    put(kLayout.handler_off, kLayout.handler_size, action.handler);
    put(kLayout.flags_off, kLayout.flags_size, action.flags);
    put(kLayout.restorer_off, kLayout.restorer_size, action.restorer);
    put(kLayout.mask_off, kLayout.mask_size, action.mask.raw);
    return uaccess::CopyToUser(at, raw, sizeof(raw));
}

/// One sigset word crosses the boundary whole.
constexpr Word kSetBytes = sizeof(SigSet);

/**
 * @brief         Whether a handler word names something ring 3 owns.
 * @param[in]     handler   The word an action record carries.
 * @return        true for the reserved default and ignore words and
 *                for any address inside the user half.
 * @note          The kernel never checks the address further: a
 *                handler in an unmapped page faults on delivery, and
 *                that fault is the caller's, not the installer's.
 * @since         0.1.0
 * @ingroup       kernel_syscall
 */
bool handler_word_ok(Word handler) {
    return handler < kUserAddressLimit;
}

/**
 * @brief         Whether a stack address may hold delivery images.
 * @param[in]     start   First byte of the stretch.
 * @param[in]     bytes   How many bytes the stretch covers.
 * @return        true when the whole stretch sits inside the
 *                registered stack window.
 * @note          The sigreturn half of delivery's own guard: the
 *                context only ever lived under the interrupted stack
 *                pointer, so a "context" anywhere else is a corrupted
 *                stack, and a corrupted stack is how this task dies.
 * @since         0.1.0
 * @ingroup       kernel_syscall
 */
bool in_stack_window(Word start, Word bytes) {
    const proc::Task* const kSelf = CurrentTask();
    return kSelf != nullptr && kSelf->vmas.holds() && kSelf->vmas->stack_contains(start, bytes);
}

}  // namespace

long long HandleRtSigpending(const SyscallFrame& frame) {
    if (frame.rsi != sizeof(SigSet)) {
        return -kEinval;
    }
    auto*  self = CurrentTask();
    SigSet pending;
    {
        const arch::IrqGuard kIrq;
        const auto           kGuard = self->signals->gate.guard();
        pending                     = self->signals->pending & self->signals->blocked;
    }
    return uaccess::CopyToUser(frame.rdi, &pending, sizeof(pending)) ? 0 : -kEfault;
}

long long HandleRtSigaction(const SyscallFrame& frame) {
    const int kNumber = static_cast<int>(frame.rdi);
    if (!proc::SignalCatchable(kNumber) || frame.r10 != kSetBytes) {
        return -kEinval;
    }
    SigAction wanted{};
    bool      install = false;
    if (frame.rsi != 0) {
        if (!read_action(frame.rsi, wanted)) {
            return -kEfault;
        }
        if (!handler_word_ok(wanted.handler)) {
            return -kEinval;
        }
        wanted.mask = MaskableSignals(wanted.mask);
        install     = true;
    }
    proc::Task& self = *CurrentTask();
    SigAction   before{};
    {
        const arch::IrqGuard kIrq;
        const auto           kGate = self.signals->gate.guard();
        before                     = self.signals->actions[kNumber - 1];
    }
    if (frame.rdx != 0 && !write_action(frame.rdx, before)) {
        return -kEfault;
    }
    if (install) {
        const arch::IrqGuard kIrq;
        const auto           kGate         = self.signals->gate.guard();
        self.signals->actions[kNumber - 1] = wanted;
    }
    return 0;
}

long long HandleRtSigprocmask(const SyscallFrame& frame) {
    if (frame.r10 != kSetBytes) {
        return -kEinval;
    }
    SigSet wanted{};
    bool   apply = false;
    if (frame.rsi != 0) {
        if (frame.rdi > kSigSetmask) {
            return -kEinval;
        }
        if (!uaccess::CopyFromUser(&wanted, frame.rsi, kSetBytes)) {
            return -kEfault;
        }
        wanted = MaskableSignals(wanted);
        apply  = true;
    }
    proc::Task& self = *CurrentTask();
    if (frame.rdx != 0) {
        const SigSet kNow = self.signals->blocked;
        if (!uaccess::CopyToUser(frame.rdx, &kNow, kSetBytes)) {
            return -kEfault;
        }
    }
    if (!apply) {
        return 0;
    }
    switch (frame.rdi) {
    case kSigBlock:
        self.signals->blocked = self.signals->blocked | wanted;
        break;
    case kSigUnblock:
        self.signals->blocked = self.signals->blocked & ~wanted;
        break;
    default:
        self.signals->blocked = wanted;
        break;
    }
    return 0;
}

long long HandleRtSigreturn(const SyscallFrame& frame) {
    const Word kContext = frame.user_rsp;
    if (!in_stack_window(kContext, sizeof(proc::SigReturnContext)) || (kContext & 0xFULL) != 0) {
        proc::Scheduler::self().exit_current(kSigSegv);
        return 0;
    }
    proc::SigReturnContext image{};
    if (!uaccess::CopyFromUser(&image, kContext, sizeof(image))) {
        proc::Scheduler::self().exit_current(kSigSegv);
        return 0;
    }
    auto& mutable_frame             = const_cast<SyscallFrame&>(frame);
    mutable_frame.user_rsp          = image.user_rsp;
    mutable_frame.rcx               = image.rcx;
    mutable_frame.r11               = image.r11;
    mutable_frame.rax               = image.rax;
    mutable_frame.rdi               = image.rdi;
    mutable_frame.rsi               = image.rsi;
    mutable_frame.rdx               = image.rdx;
    mutable_frame.r10               = image.r10;
    mutable_frame.r8                = image.r8;
    mutable_frame.r9                = image.r9;
    mutable_frame.rbx               = image.rbx;
    mutable_frame.rbp               = image.rbp;
    mutable_frame.r12               = image.r12;
    mutable_frame.r13               = image.r13;
    mutable_frame.r14               = image.r14;
    mutable_frame.r15               = image.r15;
    CurrentTask()->signals->blocked = image.old_blocked;
    return static_cast<long long>(image.rax);
}

}  // namespace cinux::syscall
