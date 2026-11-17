/**
 * @file    cow_repair.hpp
 * @brief    The armv7 write-fault repair seam every world shares.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::mm::cow {

/**
 * @brief         Repair a write fault against a present mapping.
 *
 * @param[in]     root     Page-table root the fault happened under.
 * @param[in]     address  The byte the write touched; low bits ignored.
 * @return        true when a private writable copy now backs the page
 *                and the instruction may re-run; false when the fault
 *                is nobody's to fix here (absent leaf, or a writable
 *                leaf that still faulted).
 * @note          A shared frame (ledger says two or more owners) leaves
 *                the ledger with one fewer owner; an exclusive
 *                read-only frame is replaced outright. Runs at fault
 *                depth: no sleeping, one frame of allocation.
 * @warning       None
 * @throws        None
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
[[nodiscard]] bool RepairWriteFault(unsigned long root, unsigned long address);

/**
 * @brief         Opens or closes the repair trace.
 * @param[in]     on  True prints each repair's page; false stays
 *                    silent. The station self check is the only
 *                    caller: its verdicts listen for the words.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
void SetTraceRepairs(bool on);

}  // namespace cinux::mm::cow
