/**
 * @file    cow_ledger.hpp
 * @brief   The armv7 share ledger for frames that more than one world owns.
 *
 * An exclusive frame needs no bookkeeping: releasing it returns it to the
 * allocator. A shared frame does: the first release must leave it alone.
 * Only shared frames appear here, so the table is tiny and lives in bss.
 * The neutral cow header gains this name once the fault path uses it.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::mm::cow {

/// Records one more owner for a frame; the second owner makes it shared.
void RetainFrame(unsigned long long frame);

/// Drops one owner; the last release returns the frame to the allocator.
void ReleaseFrame(unsigned long long frame);

/// Reports the recorded owners, 0 when the frame is exclusive.
[[nodiscard]] unsigned long FrameShares(unsigned long long frame);

}  // namespace cinux::mm::cow
