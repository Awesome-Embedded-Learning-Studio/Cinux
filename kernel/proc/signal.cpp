/**
 * @file kernel/proc/signal.cpp
 * @brief Signal disposition lookup helpers (F3-M1 batch 1)
 *
 * Implements the default-disposition table and the uncatchable predicate.
 * Delivery (signal_send / signal_check_and_deliver) and the default-action
 * execution arrive in batch 2.
 *
 * Namespace: cinux::proc
 */

#include "kernel/proc/signal.hpp"

namespace cinux::proc {

// POSIX default dispositions.  Unlisted signals default to Terminate.
SigDefault signal_default_action(Signal sig) {
    switch (sig) {
    case Signal::kSigquit:
    case Signal::kSigill:
    case Signal::kSigtrap:
    case Signal::kSigabrt:
    case Signal::kSigbus:
    case Signal::kSigfpe:
    case Signal::kSigsegv:
        return SigDefault::kCoreDump;
    case Signal::kSigchld:
        return SigDefault::kIgnore;
    case Signal::kSigcont:
        return SigDefault::kContinue;
    case Signal::kSigstop:
    case Signal::kSigtstp:
    case Signal::kSigttin:
    case Signal::kSigtou:
        return SigDefault::kStop;
    default:
        return SigDefault::kTerminate;
    }
}

bool signal_is_uncatchable(Signal sig) {
    return sig == Signal::kSigkill || sig == Signal::kSigstop;
}

}  // namespace cinux::proc
