/**
 * @file    futex.hpp
 * @brief   The wait queue keyed by a user word, as ring 3 sees it.
 *
 * Futex is the handshake pthread_join and the mutex family stand on:
 * a thread names a user-space word and an expected value, the kernel
 * parks it only while the word still holds that value, and a wake
 * side releases waiters by naming the same word. The death side of
 * that handshake is here too: a dying thread leaves a zero in the
 * word its joiner watches and releases it. This header is the
 * announce face only — three calls; the bucket table and the
 * matching key live in the implementation file.
 *
 * @author  Charliechen114514
 * @date    2026-10-17
 * @version 0.2
 * @since   0.8.0
 * @ingroup kernel_proc
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "cinux/result.hpp"

namespace cinux::proc {
struct Task;
}  // namespace cinux::proc

namespace cinux::proc::futex {

/**
 * @brief         Park the caller while a user word holds a value.
 * @param[in]     uaddr      The user word's address, four-byte aligned.
 * @param[in]     expected   The value to hold the caller for.
 * @param[in]     bitset     Bits this waiter watches; a waker must
 *                           intersect them. Defaults to every bit,
 *                           which is what plain FUTEX_WAIT plants.
 * @return        kOk once another side woke the caller, kWouldBlock
 *                when the word moved on before the park,
 *                kInvalidArgument when the read through the door
 *                failed.
 * @note          The word is re-read through the accessor door while
 *                the bucket lock is held, not taken on trust from a
 *                read the caller made before the lock: a wake that
 *                lands between the caller's read and the lock would
 *                otherwise be lost, and the waiter would sleep on a
 *                word that already moved. The bitset is recorded on
 *                the task before it joins the bucket, so a waker never
 *                sees a waiter whose mask is still unset.
 * @since         0.8.0
 * @ingroup       kernel_proc
 */
base::KernelError ParkOnWord(unsigned long long uaddr, unsigned int expected,
                             unsigned int bitset = ~0U);
/**
 * @brief         Parks the caller on a user word after reading it through
 *                the accessor door.
 *
 * @param[in]     uaddr     The word's user address.
 * @param[in]     expected  The value the caller believes the word holds.
 * @param[in]     bitset    Bits this waiter watches, every bit by default.
 * @return        Ok when woken; WouldBlock when the word already moved;
 *                InvalidArgument when the door refuses the address.
 * @note          The comparison happens inside ParkOnWord, under the
 *                bucket lock, against a fresh read. The SMAP window
 *                stays a single annotated move around that read; the
 *                bucket lock is held across it for a few instructions.
 * @since         0.3.0
 * @ingroup       kernel_proc
 */
base::KernelError WaitOnUserWord(unsigned long long uaddr, unsigned int expected,
                                 unsigned int bitset = ~0U);

/**
 * @brief         Wake up to a count of waiters parked on one word.
 * @param[in]     uaddr    The word whose waiters should resume.
 * @param[in]     count    At most this many waiters wake.
 * @param[in]     bitset   Bits the waker offers; a waiter wakes only
 *                         when its own mask intersects these. Defaults
 *                         to every bit, which is what plain
 *                         FUTEX_WAKE offers.
 * @return        How many were woken; zero when nobody waits there.
 * @note          Matching is by exact address, not by bucket — two
 *                words sharing a bucket never wake each other — and
 *                now also by bitset intersection, the wait/wake
 *                bitset pair's whole point: one word can carry several
 *                independent wakeup channels.
 * @since         0.8.0
 * @ingroup       kernel_proc
 */
long long WakeUserWord(unsigned long long uaddr, unsigned long long count,
                       unsigned int bitset = ~0U);

/**
 * @brief         Release a dying task's join word: zero it, wake one.
 * @param[in]     dying   The task on its way out of the machine.
 * @return        None.
 * @note          Runs while the dying task's own address space is
 *                still the active one, so the store lands in the world
 *                its joiner shares. A word the task never named, one
 *                outside the user half, or one no writable promise
 *                covers is left untouched — a dying task must not
 *                fault. The word is per address space, so a process
 *                that clears its own private copy simply goes unseen.
 * @since         0.9.0
 * @ingroup       kernel_proc
 */
void ClearTidWord(Task& dying);

}  // namespace cinux::proc::futex
