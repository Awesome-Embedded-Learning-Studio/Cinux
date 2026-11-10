/**
 * @file    file_lock.hpp
 * @brief   flock(2) locks: one registry over node identity.
 *
 * A flock lock belongs to an open file description, not to the
 * descriptor number and not to the task: two descriptors that name
 * one open (a fork, a dup) share the lock, two separate opens of one
 * node fight over it. The registry keys every held lock by the node
 * identity the open carries — which backend instance, which inode
 * number — and by the OpenFile that owns it. Shared readers coexist;
 * an exclusive lock conflicts with any other holder. Granting a new
 * lock to an owner replaces the one it held, so one call upgrades or
 * downgrades in place. The blocking half lives one floor up in the
 * flock syscall, which retries against kWouldBlock; this floor only
 * answers what is grantable right now. Locks die two ways: an
 * explicit LOCK_UN, or the last descriptor of the owning open
 * closing — the registry chains itself onto the open's final-close
 * observer for that, keeping any previously installed observer.
 *
 * @author  Charliechen114514
 * @date    2026-10-31
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_fs
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <atomic>

#include "cinux/container/slot_table.hpp"
#include "cinux/result.hpp"
#include "cinux/singleton.hpp"
#include "cinux/spin_mutex.hpp"
#include "kernel/fs/open_file.hpp"

namespace cinux::fs {

/// The flock(2) operation words, as Linux hands them to the kernel.
inline constexpr unsigned long kFlockSh = 1;  ///< Take a shared lock.
inline constexpr unsigned long kFlockEx = 2;  ///< Take an exclusive lock.
inline constexpr unsigned long kFlockNb = 4;  ///< Do not block; report conflict instead.
inline constexpr unsigned long kFlockUn = 8;  ///< Drop the open's lock on this node.

/// How many locks the registry holds at once; one open holds at most one.
inline constexpr unsigned long kFileLockMax = 32;

/// How many opens carry a final-close observer; one armed open takes one slot.
inline constexpr unsigned long kLockHookMax = 32;

/**
 * @brief   The flock registry: who holds what over which node.
 * @note    A singleton on purpose — every open in the machine fights
 *          over the same table. Fixed slots keep the object
 *          constant-initializable, so it can sit in the singleton's
 *          constinit storage without a constructor story.
 * @since   0.1.0
 * @ingroup kernel_fs
 */
class FileLocks final : public base::Singleton<FileLocks> {
    friend class base::Singleton<FileLocks>;

public:
    FileLocks(const FileLocks&)            = delete;
    FileLocks& operator=(const FileLocks&) = delete;

    /**
     * @brief         Tries to arm one flock word for this open, right now.
     * @param[in,out] file        The calling task's open of the node.
     * @param[in]     operation   One of kFlockSh, kFlockEx, kFlockUn.
     * @return        Success, or kInvalidArgument (the operation word is
     *                none of the three), kWouldBlock (another open holds a
     *                conflicting lock), kOutOfMemory (a table is full).
     * @note          A grant replaces the lock this open already holds on
     *                the node, so SH to EX and back is one call each. The
     *                kFlockNb bit never reaches this floor; the caller
     *                reads kWouldBlock and decides.
     * @since         0.1.0
     * @ingroup       kernel_fs
     */
    [[nodiscard]] base::Result<void> attempt(OpenFile& file, unsigned long operation);

    /**
     * @brief         Drops every lock this open holds, wherever it holds them.
     * @param[in,out] file   The open letting go.
     * @note          LOCK_UN and the final-close observer share this exit;
     *          an open with no lock leaves nothing behind.
     * @since         0.1.0
     * @ingroup       kernel_fs
     */
    void release(OpenFile& file);

private:
    FileLocks() = default;

    /// One held lock: a node identity and who owns it.
    struct Held {
        void*         backend   = nullptr;  ///< The backend instance minting the node.
        unsigned long ino       = 0;        ///< The node's inode number in that backend.
        OpenFile*     owner     = nullptr;  ///< The open holding the lock.
        bool          exclusive = false;    ///< Whether other opens may share.
    };

    /// The final-close chain link for one armed open.
    struct Hook {
        OpenFile* file                    = nullptr;  ///< The armed open.
        void (*previous)(const OpenFile*) = nullptr;  ///< The observer installed before this one.
    };

    /**
     * @brief         The final-close trampoline: drop the dead open's locks, then run the chain.
     * @param[in]     file   The open whose last descriptor just closed.
     * @note          Installed by compare-exchange, so an observer that
     *                arrived earlier (a readiness registry, say) still
     *                runs after this one finishes. Observers that arrive
     *                later and store blindly overwrite the chain — the
     *                same hazard every observer here shares today.
     * @since         0.1.0
     * @ingroup       kernel_fs
     */
    static void final_close(const OpenFile* file);

    /// Whether the held lock names the same node as the open, under this gate.
    [[nodiscard]] static bool names_node(const Held& held, const OpenFile& file);

    /// Erases every held lock the open owns; the caller holds the gate.
    void drop_held(const OpenFile& file);

    /// Arms the final-close chain for a freshly granted open; the caller holds the gate.
    void arm_final_close(OpenFile& file);

    base::container::SlotTable<Held, kFileLockMax> held_;
    base::container::SlotTable<Hook, kLockHookMax> hooks_;
    base::SpinMutex                                gate_;
};

}  // namespace cinux::fs
