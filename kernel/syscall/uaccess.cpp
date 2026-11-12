/**
 * @file    uaccess.cpp
 * @brief   The door's body: range first, then the annotated move.
 *
 * @author  Charliechen114514
 * @date    2026-10-23
 * @version 0.3.0
 * @since   0.3.0
 * @ingroup kernel_syscall
 * @copyright Copyright (c) 2026
 */

#include "kernel/syscall/uaccess.hpp"

#include "kernel/arch/user_access.hpp"
#include "kernel/syscall/syscall_internal.hpp"

namespace cinux::syscall::uaccess {

void ArmWindows(bool supported) {
    cinux::arch::ArmUserWindows(supported);
}

// NOLINTNEXTLINE(misc-const-correctness) destination is written through the annotated asm copy
bool CopyFromUser(void* destination, Word source, Word bytes) {
    if (bytes == 0) {
        return true;
    }
    if (!UserRangeReadable(source, bytes)) {
        return false;
    }
    return cinux::arch::MoveUserBytes(reinterpret_cast<unsigned long long>(destination), source,
                                    bytes);
}

bool CopyToUser(Word destination, const void* source, Word bytes) {
    if (bytes == 0) {
        return true;
    }
    if (!UserRangeWritable(destination, bytes)) {
        return false;
    }
    return cinux::arch::MoveUserBytes(destination, reinterpret_cast<unsigned long long>(source),
                                    bytes);
}

}  // namespace cinux::syscall::uaccess
