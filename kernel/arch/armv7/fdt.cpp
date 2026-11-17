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

/// The node name in the blob may carry an @unit suffix; the path the
/// caller spells never does.
bool ComponentMatches(const char* name, const char* low, std::uint32_t span) {
    for (std::uint32_t index = 0; index < span; ++index) {
        if (name[index] != low[index]) {
            return false;
        }
        if (name[index] == '\0') {
            return low[index] == '\0';
        }
    }
    return name[span] == '\0' || name[span] == '@';
}

/// Splits the next component off a leading '/', leaving the cursor at
/// the one after it.
bool SplitNextComponent(const char*& path, const char*& low, std::uint32_t& span) {
    if (path == nullptr || path[0] != '/') {
        return false;
    }
    ++path;
    low = path;
    while (*path != '\0' && *path != '/') {
        ++path;
    }
    span = static_cast<std::uint32_t>(path - low);
    return span != 0;
}

bool BlobLooksValid(const std::uint8_t* base, std::uint32_t& off_struct,
                    std::uint32_t& off_strings) {
    if (ReadBig32(base) != kFdtMagic) {
        return false;
    }
    const std::uint32_t total = ReadBig32(base + 4);
    off_struct               = ReadBig32(base + 8);
    off_strings              = ReadBig32(base + 12);
    return off_struct < total && off_strings < total;
}

}  // namespace

bool ReadProperty(std::uint32_t blob, const char* node_path, const char* property,
                  PropertyBytes& out) {
    const auto* base = reinterpret_cast<const std::uint8_t*>(blob);
    if (node_path == nullptr || property == nullptr || node_path[0] != '/') {
        return false;
    }
    std::uint32_t off_struct  = 0;
    std::uint32_t off_strings = 0;
    if (!BlobLooksValid(base, off_struct, off_strings)) {
        return false;
    }

    const char*   wanted  = node_path;
    const char*   low     = nullptr;
    std::uint32_t span    = 0;
    const bool    kAtRoot = wanted[1] == '\0';
    if (!kAtRoot && !SplitNextComponent(wanted, low, span)) {
        return false;
    }
    bool at_wanted = kAtRoot;
    bool seen_root = false;  // the root node's name is empty; never a side branch

    const std::uint8_t* cursor  = base + off_struct;
    const std::uint8_t* strings = base + off_strings;

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
            cursor += Align4(static_cast<std::uint32_t>(Length(name) + 1U));
            if (!seen_root) {
                seen_root = true;
                continue;
            }
            if (at_wanted) {
                continue;  // a nested node below the wanted one
            }
            if (ComponentMatches(name, low, span)) {
                if (*wanted == '\0') {
                    at_wanted = true;
                } else if (SplitNextComponent(wanted, low, span)) {
                    // still descending toward the wanted node
                } else {
                    return false;
                }
                continue;
            }
            // A side branch: skip its whole subtree by depth counting.
            std::uint32_t depth = 1;
            while (depth != 0) {
                const std::uint32_t inner = ReadBig32(cursor);
                cursor += 4;
                if (inner == kFdtBeginNode) {
                    const auto* inner_name = reinterpret_cast<const char*>(cursor);
                    cursor += Align4(static_cast<std::uint32_t>(Length(inner_name) + 1U));
                    ++depth;
                } else if (inner == kFdtEndNode) {
                    --depth;
                } else if (inner == kFdtProp) {
                    cursor += 8 + Align4(ReadBig32(cursor));
                } else if (inner == kFdtEnd) {
                    return false;
                }
            }
            continue;
        }
        if (token == kFdtEndNode) {
            return false;  // left the wanted node without the property
        }
        if (token != kFdtProp) {
            return false;
        }
        const std::uint32_t len      = ReadBig32(cursor);
        const std::uint32_t name_off = ReadBig32(cursor + 4);
        const std::uint8_t* value    = cursor + 8;
        cursor += 8 + Align4(len);
        if (!at_wanted) {
            continue;
        }
        const char* found = reinterpret_cast<const char*>(strings + name_off);
        if (Equals(found, property)) {
            out.bytes = value;
            out.len   = len;
            return true;
        }
    }
}

bool ReadMemoryRange(std::uint32_t blob, MemoryRange& range) {
    // The memory node is found by name, not by path: the board may
    // spell it memory@80000000, and the unit address belongs to the
    // firmware, not to this reader.
    const auto* base = reinterpret_cast<const std::uint8_t*>(blob);
    std::uint32_t off_struct  = 0;
    std::uint32_t off_strings = 0;
    if (!BlobLooksValid(base, off_struct, off_strings)) {
        return false;
    }
    const std::uint8_t* cursor  = base + off_struct;
    const std::uint8_t* strings = base + off_strings;
    bool in_memory = false;
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
            in_memory = ComponentMatches(name, "memory", 6);
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
        const std::uint32_t len      = ReadBig32(cursor);
        const std::uint32_t name_off = ReadBig32(cursor + 4);
        const std::uint8_t* value    = cursor + 8;
        cursor += 8 + Align4(len);
        if (!in_memory) {
            continue;
        }
        const char* found = reinterpret_cast<const char*>(strings + name_off);
        if (Equals(found, "reg") && len >= 8) {
            range.base = ReadBig32(value);
            range.size = ReadBig32(value + 4);
            return true;
        }
    }
}

}  // namespace cinux::armv7
