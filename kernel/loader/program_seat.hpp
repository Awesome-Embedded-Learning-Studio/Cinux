/**
 * @file    program_seat.hpp
 * @brief   The one ladder every program image climbs into its world.
 *
 * Three callers used to assemble the same steps by hand: the first
 * shell the kernel seats (init thread), the exec syscall replacing a
 * running task, and the armv7 bring-up lane seating its guest probe.
 * The assembly drifted -- each copy had its own idea of which facts
 * fields get filled -- so the ladder lives here once. The caller keeps
 * only what is genuinely theirs: where the image bytes came from, the
 * bias policy, and the argument block.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_loader
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/fs/vfs.hpp"
#include "kernel/loader/loader.hpp"
#include "kernel/mm/address_space.hpp"
#include "kernel/mm/vma.hpp"

namespace cinux::loader {

/// What a seated program hands back: the entry picture for the stack
/// builder, plus the one fact the stack layout needs (whether the GNU
/// stack segment asked for execute permission).
struct ProgramSeat {
    EntryFacts facts{};            ///< Entry picture with page_bytes and platform filled.
    bool       stack_executable{}; ///< The GNU stack flag carried kSegmentExec.
    std::uint64_t start_entry{};   ///< Where the CPU starts: the interpreter's
                                   ///< entry for a dynamic image, AT_ENTRY's
                                   ///< value for a static one.
};

/**
 * @brief         Seats one ELF image into a fresh world.
 *
 * @param[in]     image   The image bytes; a prefix of at least the
 *                        headers must be readable.
 * @param[in]     bytes   How many bytes of image are readable.
 * @param[in]     node    The file node the image came from.
 * @param[in]     view    The resolve view serving the node's bytes.
 * @param[in,out] space   The freshly built world pages land in.
 * @param[in,out] vmas    The range register segments report to.
 * @param[in]     base    Where the image lands; zero serves programs
 *                        linked at absolute addresses, a caller's slide
 *                        serves the relocatable shape. The bias policy
 *                        itself is the caller's: bring-up lanes want a
 *                        fixed base, exec wants a random one.
 * @return        The seat, or kInvalidArgument (shape), kIOError
 *                (headers unreadable), kOutOfMemory (world refused a
 *                segment, or the interpreter refused to load).
 * @note          A static image takes the interpreter half as a no-op;
 *                the returned entry is then the program's own.
 * @since         0.1.0
 * @ingroup       kernel_loader
 */
[[nodiscard]] base::Result<ProgramSeat> SeatProgram(
    const unsigned char* image, unsigned long bytes, const fs::FsNode& node,
    const fs::FsView& view, mm::AddressSpace& space, mm::VmaStore& vmas,
    std::uint64_t base = 0);

/// Draws the sixteen random bytes every entry picture carries, one
/// word at a time with a blocking retry after a failed fast draw.
/// @param[out] bytes  The sixteen-byte destination.
/// @since  0.2.0
void DrawEntryRandom(unsigned char (&bytes)[16]);

/// The stack top every seated program leaves on: the fixed user
/// program top less one page-aligned slide, so stacked worlds do not
/// share a rim.
/// @since  0.2.0
[[nodiscard]] unsigned long long DrawEntryStackTop();

}  // namespace cinux::loader
