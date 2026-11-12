#include "kernel/loader/elf_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "cinux/byte_order.hpp"
#include "cinux/result.hpp"
#include "kernel/arch/abi.hpp"

namespace cinux::loader {

namespace {

constexpr std::size_t  kIdentBytes   = 16;
constexpr std::size_t  kEhdr32Bytes  = 52;
constexpr std::size_t  kEhdr64Bytes  = 64;
constexpr std::size_t  kPhdr32Bytes  = 32;
constexpr std::size_t  kPhdr64Bytes  = 56;
constexpr std::uint8_t kClass32      = 1;
constexpr std::uint8_t kClass64      = 2;
constexpr std::uint8_t kDataLittle   = 1;

// Program-header field offsets. The two classes lay a program header out
// differently, so every read below picks its offset from the image's own
// class rather than assuming the one this machine happens to run.
constexpr std::size_t kPhdrType     = 0;
constexpr std::size_t kPhdrFlags64  = 4;
constexpr std::size_t kPhdrOffset64 = 8;
constexpr std::size_t kPhdrVaddr64  = 16;
constexpr std::size_t kPhdrFilesz64 = 32;
constexpr std::size_t kPhdrMemsz64  = 40;
constexpr std::size_t kPhdrOffset32 = 4;
constexpr std::size_t kPhdrVaddr32  = 8;
constexpr std::size_t kPhdrFilesz32 = 16;
constexpr std::size_t kPhdrMemsz32  = 20;
constexpr std::size_t kPhdrFlags32  = 24;

bool IsWide(const ElfImage& image) {
    return image.elf_class == kClass64;
}

std::uint64_t ReadAddress(const std::uint8_t* at, std::size_t offset, bool wide) {
    return wide ? base::ReadWord64(at, offset) : base::ReadWord32(at, offset);
}

std::uint64_t SegmentOffset(const ElfImage& image, const std::uint8_t* at) {
    return ReadAddress(at, IsWide(image) ? kPhdrOffset64 : kPhdrOffset32, IsWide(image));
}

std::uint64_t SegmentFileBytes(const ElfImage& image, const std::uint8_t* at) {
    return ReadAddress(at, IsWide(image) ? kPhdrFilesz64 : kPhdrFilesz32, IsWide(image));
}

std::uint32_t SegmentFlags(const ElfImage& image, const std::uint8_t* at) {
    return base::ReadWord32(at, IsWide(image) ? kPhdrFlags64 : kPhdrFlags32);
}

const std::uint8_t* SegmentAt(const ElfImage& image, std::uint16_t index) {
    return image.segments + (static_cast<std::size_t>(index) *
                             static_cast<std::size_t>(image.segment_size));
}

}  // namespace

base::Result<ElfImage> ParseElf(const std::uint8_t* data, std::size_t bytes, ImageRole role) {
    if (data == nullptr || bytes < kEhdr32Bytes) {
        return base::KernelError::kInvalidArgument;
    }
    for (unsigned int index = 0; index < 4; ++index) {
        if (data[index] != kElfMagic[index]) {
            return base::KernelError::kInvalidArgument;
        }
    }
    const std::uint8_t kClass = data[4];
    if ((kClass != kClass32 && kClass != kClass64) || data[5] != kDataLittle) {
        return base::KernelError::kInvalidArgument;
    }
    const bool         kWide = kClass == kClass64;
    const std::size_t  kHeaderBytes = kWide ? kEhdr64Bytes : kEhdr32Bytes;
    const std::size_t  kEntryBytes  = kWide ? kPhdr64Bytes : kPhdr32Bytes;
    if (bytes < kHeaderBytes) {
        return base::KernelError::kInvalidArgument;
    }
    const std::uint16_t kType    = base::ReadWord16(data, kIdentBytes);
    const std::uint16_t kMachine = base::ReadWord16(data, kIdentBytes + 2);
    const bool          kTypeOk  = role == ImageRole::kInterpreter
                                       ? kType == kTypeShared
                                       : kType == kTypeExecutable || kType == kTypeShared;
    if (!kTypeOk) {
        return base::KernelError::kInvalidArgument;
    }
    const std::uint64_t kEntry      = kWide ? base::ReadWord64(data, 24) : base::ReadWord32(data, 24);
    const std::uint64_t kPhdrOffset =
        kWide ? base::ReadWord64(data, 32) : base::ReadWord32(data, 28);
    const std::uint16_t kPhdrSize =
        base::ReadWord16(data, kWide ? 54 : 42);
    const std::uint16_t kPhdrCount =
        base::ReadWord16(data, kWide ? 56 : 44);
    if (kPhdrSize < kEntryBytes || kPhdrCount == 0) {
        return base::KernelError::kInvalidArgument;
    }
    const std::uint64_t kTableBytes = static_cast<std::uint64_t>(kPhdrSize) * kPhdrCount;
    if (kPhdrOffset > bytes || kTableBytes > bytes - kPhdrOffset) {
        return base::KernelError::kInvalidArgument;
    }
    ElfImage image{
        .entry           = kEntry,
        .headers_address = 0,
        .segments        = data + kPhdrOffset,
        .segment_count   = kPhdrCount,
        .segment_size    = kPhdrSize,
        .elf_class       = kClass,
        .machine         = kMachine,
    };
    for (std::uint16_t index = 0; index < kPhdrCount; ++index) {
        const std::uint8_t* const kAt = SegmentAt(image, index);
        if (base::ReadWord32(kAt, kPhdrType) != kProgramLoad) {
            continue;
        }
        const std::uint64_t kOffset = SegmentOffset(image, kAt);
        const std::uint64_t kFiles  = SegmentFileBytes(image, kAt);
        if (kOffset <= kPhdrOffset && kPhdrOffset < kOffset + kFiles) {
            image.headers_address =
                ReadAddress(kAt, kWide ? kPhdrVaddr64 : kPhdrVaddr32, kWide) +
                (kPhdrOffset - kOffset);
            break;
        }
    }
    return image;
}

bool ImageRunsHere(const ElfImage& image) {
    return image.elf_class == cinux::arch::abi::kElfClass &&
           image.machine == cinux::arch::abi::kElfMachine;
}

bool ReadSegment(const ElfImage& image, std::uint16_t index, ElfSegment& segment) {
    if (index >= image.segment_count) {
        return false;
    }
    const std::uint8_t* const kAt    = SegmentAt(image, index);
    const bool                kWide  = IsWide(image);
    if (base::ReadWord32(kAt, kPhdrType) != kProgramLoad) {
        return false;
    }
    segment = ElfSegment{
        .virtual_address = ReadAddress(kAt, kWide ? kPhdrVaddr64 : kPhdrVaddr32, kWide),
        .file_offset     = ReadAddress(kAt, kWide ? kPhdrOffset64 : kPhdrOffset32, kWide),
        .file_bytes      = ReadAddress(kAt, kWide ? kPhdrFilesz64 : kPhdrFilesz32, kWide),
        .memory_bytes    = ReadAddress(kAt, kWide ? kPhdrMemsz64 : kPhdrMemsz32, kWide),
        .flags           = SegmentFlags(image, kAt),
    };
    return true;
}

bool ReadInterpreterPath(const std::uint8_t* data, std::size_t bytes, char* path,
                         std::size_t path_bytes) {
    const base::Result<ElfImage> kParsed = ParseElf(data, bytes, ImageRole::kProgram);
    if (!kParsed.ok() || path == nullptr || path_bytes == 0) {
        return false;
    }
    const ElfImage kImage = kParsed.value();
    for (std::uint16_t index = 0; index < kImage.segment_count; ++index) {
        const std::uint8_t* const kAt = SegmentAt(kImage, index);
        if (base::ReadWord32(kAt, kPhdrType) != kProgramInterp) {
            continue;
        }
        const std::uint64_t kOffset = SegmentOffset(kImage, kAt);
        const std::uint64_t kFiles  = SegmentFileBytes(kImage, kAt);
        if (kOffset > bytes || kFiles > bytes - kOffset) {
            return false;
        }
        const std::uint64_t kCopy = std::min(kFiles, static_cast<std::uint64_t>(path_bytes - 1));
        for (std::uint64_t done = 0; done < kCopy; ++done) {
            path[done] = static_cast<char>(data[kOffset + done]);
        }
        path[kCopy] = '\0';
        return true;
    }
    return false;
}

bool ReadGnuStackFlags(const std::uint8_t* data, std::size_t bytes, std::uint32_t& flags) {
    const base::Result<ElfImage> kParsed = ParseElf(data, bytes, ImageRole::kProgram);
    if (!kParsed.ok()) {
        return false;
    }
    const ElfImage kImage = kParsed.value();
    for (std::uint16_t index = 0; index < kImage.segment_count; ++index) {
        const std::uint8_t* const kAt = SegmentAt(kImage, index);
        if (base::ReadWord32(kAt, kPhdrType) != kProgramGnuStack) {
            continue;
        }
        flags = SegmentFlags(kImage, kAt);
        return true;
    }
    return false;
}

bool ProgramNeedsLoadBias(const std::uint8_t* data, std::size_t bytes) {
    const base::Result<ElfImage> kParsed = ParseElf(data, bytes, ImageRole::kProgram);
    if (!kParsed.ok()) {
        return false;
    }
    return base::ReadWord16(data, kIdentBytes) == kTypeShared;
}

}  // namespace cinux::loader
