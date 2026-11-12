/**
 * @file    libc_support.cpp
 * @brief   The C memory primitives the freestanding armv7 image needs.
 *
 * GCC emits calls to these from ordinary code (aggregate zeroing, struct
 * copies), and nothing in a -nostdlib image provides them.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include <cstddef>
#include <cstdint>

extern "C" void* memset(void* destination, int value, std::size_t bytes) {
    auto*       out  = static_cast<unsigned char*>(destination);
    const auto  fill = static_cast<unsigned char>(value);
    for (std::size_t index = 0; index < bytes; ++index) {
        out[index] = fill;
    }
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, std::size_t bytes) {
    auto*       out = static_cast<unsigned char*>(destination);
    const auto* in  = static_cast<const unsigned char*>(source);
    for (std::size_t index = 0; index < bytes; ++index) {
        out[index] = in[index];
    }
    return destination;
}

extern "C" void* memmove(void* destination, const void* source, std::size_t bytes) {
    auto*       out = static_cast<unsigned char*>(destination);
    const auto* in  = static_cast<const unsigned char*>(source);
    if (out < in) {
        for (std::size_t index = 0; index < bytes; ++index) {
            out[index] = in[index];
        }
    } else {
        for (std::size_t index = bytes; index > 0; --index) {
            out[index - 1] = in[index - 1];
        }
    }
    return destination;
}

extern "C" int memcmp(const void* left, const void* right, std::size_t bytes) {
    const auto* a = static_cast<const unsigned char*>(left);
    const auto* b = static_cast<const unsigned char*>(right);
    for (std::size_t index = 0; index < bytes; ++index) {
        if (a[index] != b[index]) {
            return a[index] < b[index] ? -1 : 1;
        }
    }
    return 0;
}
