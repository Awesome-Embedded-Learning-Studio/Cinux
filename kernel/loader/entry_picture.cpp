#include <array>
#include <initializer_list>

#include "cinux/memory.hpp"
#include "kernel/arch/x86_64/page.hpp"
#include "kernel/loader/loader.hpp"

namespace cinux::loader {

namespace {

using arch::page::kSize;

/// One word of the exec initial-stack ABI. The x86_64 System V startup
/// stack speaks 8-byte words whatever the kernel build's own word width
/// is; the arch split will move this alias next to the ABI it serves.
using AbiWord = unsigned long long;

/// Bytes one initial-stack word occupies on the wire.
constexpr unsigned long kAbiWordBytes = sizeof(AbiWord);

struct PicturePlane {
    unsigned char* page;
    unsigned long  page_base;
    unsigned long  strings_at;
    unsigned long  words_at;
};

bool plane_room(const PicturePlane& plane) {
    return plane.words_at + 2 * kAbiWordBytes <= plane.strings_at;
}

void put_word(PicturePlane& plane, AbiWord word) {
    cinux::base::CopyBytes(plane.page + plane.words_at, &word, sizeof(word));
    plane.words_at += kAbiWordBytes;
}

bool put_pair(PicturePlane& plane, unsigned long type, unsigned long value) {
    if (!plane_room(plane)) {
        return false;
    }
    put_word(plane, static_cast<AbiWord>(type));
    put_word(plane, static_cast<AbiWord>(value));
    return true;
}

bool put_string(PicturePlane& plane, const char* text) {
    if (text == nullptr) {
        return false;
    }
    unsigned long length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    if (length + 1 + kAbiWordBytes + 2 * kAbiWordBytes > plane.strings_at - plane.words_at) {
        return false;
    }
    plane.strings_at -= length + 1;
    cinux::base::CopyBytes(plane.page + plane.strings_at, text, length + 1);
    const AbiWord kLanded = static_cast<AbiWord>(plane.page_base + plane.strings_at);
    cinux::base::CopyBytes(plane.page + plane.words_at, &kLanded, sizeof(kLanded));
    plane.words_at += kAbiWordBytes;
    return true;
}

unsigned long land_randomness(PicturePlane& plane, const EntryFacts& facts) {
    if (facts.randomness == nullptr || facts.random_bytes == 0 ||
        facts.random_bytes > plane.strings_at - plane.words_at - 2 * kAbiWordBytes) {
        return 0;
    }
    plane.strings_at -= facts.random_bytes;
    cinux::base::CopyBytes(plane.page + plane.strings_at, facts.randomness, facts.random_bytes);
    return plane.page_base + plane.strings_at;
}

unsigned long land_string(PicturePlane& plane, const char* text) {
    unsigned long length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    if (length + 1 + 2 * kAbiWordBytes > plane.strings_at - plane.words_at) {
        return 0;
    }
    plane.strings_at -= length + 1;
    cinux::base::CopyBytes(plane.page + plane.strings_at, text, length + 1);
    return plane.page_base + plane.strings_at;
}

struct AuxiliaryPair {
    unsigned long type;
    unsigned long value;
};

auto fixed_pairs(const EntryFacts& facts) {
    return std::array{
        AuxiliaryPair{.type = kAuxPhdr, .value = facts.program_headers},
        AuxiliaryPair{.type = kAuxPhent, .value = facts.header_entry_bytes},
        AuxiliaryPair{.type = kAuxPhnum, .value = facts.header_count},
        AuxiliaryPair{.type = kAuxPageSize, .value = facts.page_bytes},
        AuxiliaryPair{.type = kAuxBase, .value = facts.interpreter_base},
        AuxiliaryPair{.type = kAuxEntry, .value = facts.program_entry},
        AuxiliaryPair{.type = kAuxUid, .value = 0},
        AuxiliaryPair{.type = kAuxEuid, .value = 0},
        AuxiliaryPair{.type = kAuxGid, .value = 0},
        AuxiliaryPair{.type = kAuxEgid, .value = 0},
        AuxiliaryPair{.type = kAuxSecure, .value = 0},
        AuxiliaryPair{.type = kAuxHwcap, .value = facts.hwcap},
    };
}

bool put_facts(PicturePlane& plane, const EntryFacts& facts, unsigned long first_run_landed,
               unsigned long random_landed) {
    for (const auto& pair : fixed_pairs(facts)) {
        if (!put_pair(plane, pair.type, pair.value)) {
            return false;
        }
    }
    unsigned long platform_landed = 0;
    if (facts.platform != nullptr) {
        platform_landed = land_string(plane, facts.platform);
        if (platform_landed == 0) {
            return false;
        }
    }
    if (platform_landed != 0 && !put_pair(plane, kAuxPlatform, platform_landed)) {
        return false;
    }
    if (random_landed != 0 && !put_pair(plane, kAuxRandom, random_landed)) {
        return false;
    }
    if (first_run_landed != 0 && !put_pair(plane, kAuxExecfn, first_run_landed)) {
        return false;
    }
    return true;
}

}  // namespace

unsigned long EntryPictureBytes(const EntryRuns& runs, const EntryFacts& facts) {
    unsigned long count   = 0;
    unsigned long strings = 0;
    for (const auto* vector : {runs.arguments, runs.environment}) {
        if (vector == nullptr) {
            continue;
        }
        for (unsigned long index = 0; vector[index] != nullptr; ++index) {
            ++count;
            unsigned long length = 0;
            while (vector[index][length] != '\0') {
                ++length;
            }
            strings += length + 1;
        }
    }
    unsigned long pairs = fixed_pairs(facts).size() + 1;
    if (facts.platform != nullptr) {
        ++pairs;
        unsigned long length = 0;
        while (facts.platform[length] != '\0') {
            ++length;
        }
        strings += length + 1;
    }
    if (facts.randomness != nullptr && facts.random_bytes != 0) {
        ++pairs;
        strings += facts.random_bytes;
    }
    if (facts.executable != nullptr) {
        unsigned long length = 0;
        while (facts.executable[length] != '\0') {
            ++length;
        }
        strings += length + 1;
    }
    if (facts.executable != nullptr ||
        (runs.arguments != nullptr && runs.arguments[0] != nullptr)) {
        ++pairs;
    }
    return strings + ((count + 3 + (pairs * 2)) * kAbiWordBytes);
}

unsigned long WriteEntryPicture(unsigned char* stack_page, unsigned long entry_rsp,
                                const EntryRuns& runs, const EntryFacts& facts) {
    const unsigned long kPageBase = entry_rsp - (entry_rsp % kSize);
    return WriteEntryPicture(
        {.bytes = stack_page, .base = kPageBase, .capacity = kSize, .rsp = entry_rsp}, runs, facts);
}

unsigned long WriteEntryPicture(const EntryStack& stack, const EntryRuns& runs,
                                const EntryFacts& facts) {
    if (stack.rsp < stack.base || stack.rsp - stack.base > stack.capacity ||
        EntryPictureBytes(runs, facts) > stack.capacity - (stack.rsp - stack.base)) {
        return 0;
    }
    PicturePlane plane{
        .page       = stack.bytes,
        .page_base  = stack.base,
        .strings_at = stack.capacity,
        .words_at   = stack.rsp - stack.base,
    };
    const unsigned long kRandomLanded = land_randomness(plane, facts);
    unsigned long       argc          = 0;
    while (runs.arguments != nullptr && runs.arguments[argc] != nullptr) {
        ++argc;
    }
    unsigned long envc = 0;
    if (runs.environment != nullptr) {
        while (runs.environment[envc] != nullptr) {
            ++envc;
        }
    }
    put_word(plane, argc);
    unsigned long first_run_landed = 0;
    for (unsigned long index = 0; index < argc; ++index) {
        if (!put_string(plane, runs.arguments[index])) {
            return 0;
        }
        if (index == 0) {
            first_run_landed = plane.page_base + plane.strings_at;
        }
    }
    put_word(plane, 0);
    for (unsigned long index = 0; index < envc; ++index) {
        if (!put_string(plane, runs.environment[index])) {
            return 0;
        }
    }
    put_word(plane, 0);
    const unsigned long kFirstRunLanded =
        facts.executable == nullptr ? first_run_landed : land_string(plane, facts.executable);
    if (!put_facts(plane, facts, kFirstRunLanded, kRandomLanded)) {
        return 0;
    }
    put_word(plane, static_cast<unsigned long>(kAuxNull));
    put_word(plane, 0);
    return stack.rsp;
}

}  // namespace cinux::loader
