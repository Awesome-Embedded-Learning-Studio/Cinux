/**
 * @file    user_access.hpp
 * @brief   The x86-64 door into user memory: the SMAP window, the
 *          annotated byte move, and its exception-table fixups.
 *
 * A user copy runs through rep movsb, with an exception-table entry
 * planted against the move so a fault mid-copy lands on a fixup word
 * that shuts the SMAP window, clears the verdict, and rejoins the normal
 * exit. Which instruction carries the move, and how the recovery point
 * is expressed, is the port's business.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::arch {

namespace user_access {

/// The door's own state, one home for the arm bit; Meyers singleton,
/// no bare globals. stac and clac fault with #UD on a processor whose
/// CPUID answer lacks SMAP, so the copies reach for the window only
/// when this bit says the machine has one.
class DoorState {
public:
    static DoorState& self() {
        static DoorState state;
        return state;
    }

    /// The boot's CPUID answer for SMAP, remembered so no copy pays
    /// for asking twice.
    bool windows_armed = false;

private:
    DoorState() = default;
};

/**
 * @brief         Runs one door-to-door byte move, annotated.
 *
 * @param[in,out] destination   Kernel-side address, as a raw word.
 * @param[in,out] source        User-side address, as a raw word.
 * @param[in,out] bytes         Width of the move; the hardware counts
 *                              it down as the move proceeds.
 * @return        True when the whole move ran; false when the page
 *                fault path bounced it through the exception table.
 * @note          The output operands reload from the very registers the
 *                hardware walked, so the caller sees how far the move
 *                got.
 * @since         0.3.0
 * @ingroup       kernel_arch
 */
inline bool MoveUserBytes(unsigned long long destination, unsigned long long source,
                          unsigned long long bytes) {
    unsigned long long verdict = 1;  // NOLINT(misc-const-correctness) written by the asm output
    if (DoorState::self().windows_armed) {
        asm volatile(
            "stac\n"
            "1: rep movsb\n"
            "2: clac\n"
            ".pushsection .ex_table,\"a\"\n"
            ".balign 16\n"
            ".quad 1b, 3f\n"
            ".popsection\n"
            "jmp 4f\n"
            "3: clac\n"
            "xorq %q0, %q0\n"
            "4:\n"
            : "+r"(verdict), "+D"(destination), "+S"(source), "+c"(bytes)
            :
            : "memory", "cc");
        return verdict != 0;
    }
    asm volatile(
        "1: rep movsb\n"
        ".pushsection .ex_table,\"a\"\n"
        ".balign 16\n"
        ".quad 1b, 2f\n"
        ".popsection\n"
        "jmp 3f\n"
        "2: xorq %q0, %q0\n"
        "3:\n"
        : "+r"(verdict), "+D"(destination), "+S"(source), "+c"(bytes)
        :
        : "memory", "cc");
    return verdict != 0;
}

}  // namespace user_access

/**
 * @brief         Records whether this machine's SMAP windows are armed.
 * @param[in]     supported   The boot's CPUID answer for SMAP.
 * @since         0.3.0
 * @ingroup       kernel_arch
 */
inline void ArmUserWindows(bool supported) {
    user_access::DoorState::self().windows_armed = supported;
}

/**
 * @brief         Reports whether SMAP windows are armed on this machine.
 * @return        The armed bit the boot recorded.
 * @since         0.3.0
 * @ingroup       kernel_arch
 */
inline bool UserWindowsArmed() {
    return user_access::DoorState::self().windows_armed;
}

/**
 * @brief         Moves bytes between a kernel and a user address.
 * @param[in]     kernel_address   Kernel-side address.
 * @param[in]     user_address     User-side address.
 * @param[in]     bytes            How many.
 * @return        true when every byte moved.
 * @since         0.3.0
 * @ingroup       kernel_arch
 */
inline bool MoveUserBytes(unsigned long long kernel_address, unsigned long long user_address,
                          unsigned long long bytes) {
    return user_access::MoveUserBytes(kernel_address, user_address, bytes);
}

}  // namespace cinux::arch
