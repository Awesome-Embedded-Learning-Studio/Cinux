#include "kernel/arch/armv7/fdt.hpp"

namespace cinux::armv7 {

namespace {

constexpr std::uint32_t kFdtMagic       = 0xD00DFEEDU;
constexpr std::uint32_t kFdtBeginNode   = 1;
constexpr std::uint32_t kFdtEndNode     = 2;
constexpr std::uint32_t kFdtProp        = 3;
constexpr std::uint32_t kFdtNop         = 4;
constexpr std::uint32_t kFdtEnd         = 5;
constexpr std::uint32_t kHeaderBytes    = 40;

std::uint32_t ReadBig32(const void* at) {
    const auto* bytes = static_cast<const volatile std::uint8_t*>(at);
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
}

std::uint32_t Align4(std::uint32_t value) {
    return (value + 3U) & ~3U;
}

bool Equals(const char* left, const char* right) {
    while (*left != '\0' || *right != '\0') {
        if (*left != *right) {
            return false;
        }
        ++left;
        ++right;
    }
    return true;
}

std::size_t Length(const char* text) {
    std::size_t total = 0;
    while (text[total] != '\0') {
        ++total;
    }
    return total;
}

}  // namespace

bool ReadMemoryRange(std::uint32_t blob, MemoryRange& range) {
    const auto* base = reinterpret_cast<const std::uint8_t*>(blob);
    if (ReadBig32(base) != kFdtMagic) {
        return false;
    }
    const std::uint32_t total       = ReadBig32(base + 4);
    const std::uint32_t off_struct  = ReadBig32(base + 8);
    const std::uint32_t off_strings = ReadBig32(base + 12);
    if (off_struct >= total || off_strings >= total) {
        return false;
    }

    const std::uint8_t* cursor = base + off_struct;
    const std::uint8_t* strings = base + off_strings;
    bool                in_memory = false;

    for (;;) {
        const std::uint32_t token = ReadBig32(cursor);
        cursor += 4;
        if (token == kFdtEnd) {
            return false;
        }
        if (token == kFdtNop) {
            continue;
        }
        if (token == kFdtBeginNode) {
            const auto* name = reinterpret_cast<const char*>(cursor);
            in_memory        = name[0] == 'm' && name[1] == 'e' && name[2] == 'm' &&
                        name[3] == 'o' && name[4] == 'r' && name[5] == 'y';
            cursor += Align4(static_cast<std::uint32_t>(Length(name) + 1U));
            continue;
        }
        if (token == kFdtEndNode) {
            in_memory = false;
            continue;
        }
        if (token != kFdtProp) {
            return false;
        }
        // A property's blob layout is tag, len, nameoff, value, in that
        // order; len comes before the name offset.
        const std::uint32_t len      = ReadBig32(cursor);
        const std::uint32_t name_off = ReadBig32(cursor + 4);
        const std::uint8_t* value    = cursor + 8;
        cursor += 8 + Align4(len);

        if (!in_memory) {
            continue;
        }
        const char* property = reinterpret_cast<const char*>(strings + name_off);
        if (Equals(property, "reg") && len >= 8) {
            range.base = ReadBig32(value);
            range.size = ReadBig32(value + 4);
            return true;
        }
    }
}

}  // namespace cinux::armv7
