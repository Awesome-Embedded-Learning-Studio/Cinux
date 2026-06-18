/**
 * @file kernel/proc/signal.hpp
 * @brief POSIX signal vocabulary and per-task disposition (F3-M1)
 *
 * Defines the core signal numbers (Signal), the 64-bit signal set (SigSet),
 * the per-signal disposition (SigAction), and the lookup helpers for the
 * default disposition and the uncatchable predicate.  Delivery machinery
 * (signal_send / signal_check_and_deliver) and the syscall surface
 * (kill / sigaction / sigprocmask / sigreturn) arrive in later batches.
 *
 * Namespace: cinux::proc
 */

#pragma once

#include <stdint.h>

namespace cinux::proc {

// ============================================================
// Signal numbers (core POSIX subset, 1..22)
// ============================================================

enum class Signal : int {
    kSighup    = 1,   ///< Hangup detected on controlling terminal
    kSigint    = 2,   ///< Interrupt from keyboard (Ctrl+C)
    kSigquit   = 3,   ///< Quit from keyboard (Ctrl+\)
    kSigill    = 4,   ///< Illegal instruction
    kSigtrap   = 5,   ///< Trace/breakpoint trap
    kSigabrt   = 6,   ///< Abort signal from abort(3)
    kSigbus    = 7,   ///< Bus error (bad memory access)
    kSigfpe    = 8,   ///< Floating-point exception
    kSigkill   = 9,   ///< Kill (cannot be caught, blocked, or ignored)
    kSigusr1   = 10,  ///< User-defined signal 1
    kSigsegv   = 11,  ///< Invalid memory reference
    kSigusr2   = 12,  ///< User-defined signal 2
    kSigpipe   = 13,  ///< Broken pipe: write to pipe with no readers
    kSigalrm   = 14,  ///< Timer signal from alarm(2)
    kSigterm   = 15,  ///< Termination signal
    kSigstkflt = 16,  ///< Stack fault on coprocessor
    kSigchld   = 17,  ///< Child stopped or terminated
    kSigcont   = 18,  ///< Continue if stopped
    kSigstop   = 19,  ///< Stop process (cannot be caught, blocked, or ignored)
    kSigtstp   = 20,  ///< Stop typed at terminal (Ctrl+Z)
    kSigttin   = 21,  ///< Terminal input for background process
    kSigtou    = 22,  ///< Terminal output for background process
};

/// Highest supported signal number in the core set.
constexpr int kSignalMax = 22;

/// Array extent for per-signal tables (index 0 is unused; 1..kSignalMax).
constexpr int kSignalCount = kSignalMax + 1;

// ============================================================
// Signal sets (64-bit bitmask, bit n <=> signal n)
// ============================================================

using SigSet = uint64_t;

inline constexpr SigSet sig_make_mask(Signal sig) {
    return SigSet{1} << static_cast<unsigned>(sig);
}

inline void sig_set_add(SigSet& set, Signal sig) {
    set |= sig_make_mask(sig);
}

inline void sig_set_del(SigSet& set, Signal sig) {
    set &= ~sig_make_mask(sig);
}

inline bool sig_is_member(SigSet set, Signal sig) {
    return (set & sig_make_mask(sig)) != 0;
}

/// Validate a raw int as a core signal number (1..kSignalMax).
inline bool signal_valid(int num) {
    return num >= static_cast<int>(Signal::kSighup) && num <= kSignalMax;
}

// ============================================================
// Dispositions
// ============================================================

/// Default disposition when a signal is delivered with no handler installed.
enum class SigDefault : uint8_t {
    kTerminate,  ///< Terminate the process
    kCoreDump,   ///< Terminate and dump core
    kIgnore,     ///< Discard the signal
    kStop,       ///< Stop the process (job control)
    kContinue,   ///< Resume a stopped process
};

/// How a process elects to handle a given signal.
enum class HandlerType : uint8_t {
    kDefault,  ///< SIG_DFL -- apply the default disposition
    kIgnore,   ///< SIG_IGN -- discard the signal
    kCustom,   ///< A user-mode handler is installed
};

/// Per-signal disposition installed via sigaction.
struct SigAction {
    HandlerType type{HandlerType::kDefault};
    uint64_t    handler_addr{0};      ///< User-mode handler (valid when type == kCustom)
    SigSet      sa_mask{0};           ///< Extra signals blocked while the handler runs
    bool        sa_restart{false};    ///< SA_RESTART: restartable syscalls
    bool        sa_resethand{false};  ///< SA_RESETHAND: reset to default after one delivery
};

// ============================================================
// Lookup helpers (defined in signal.cpp)
// ============================================================

/// Default disposition for @p sig.
SigDefault signal_default_action(Signal sig);

/// SIGKILL and SIGSTOP cannot be caught, blocked, or ignored.
bool signal_is_uncatchable(Signal sig);

}  // namespace cinux::proc
