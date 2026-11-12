/**
 * @file    ramfs.cpp
 * @brief   The built-in file table.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/ramfs.hpp"

namespace cinux::armv7::ramfs {
namespace {

constexpr unsigned char kHelloFile[] = "hello\n";
constexpr unsigned long kHelloSize   = sizeof(kHelloFile) - 1;
constexpr unsigned char kMotdFile[]  = "hi there\n";
constexpr unsigned long kMotdSize    = sizeof(kMotdFile) - 1;
constexpr unsigned char kReadmeFile[] = "readme!\n";
constexpr unsigned long kReadmeSize   = sizeof(kReadmeFile) - 1;

unsigned char g_scratch[64];  // zeroed at boot
unsigned long g_scratch_len = 0;

struct Entry {
    const char*          name;
    unsigned long        name_length;
    const unsigned char* data;
    unsigned long        size;
};

constexpr Entry kEntries[] = {
    {"/hello", 6, kHelloFile, kHelloSize},
    {"/motd", 5, kMotdFile, kMotdSize},
    {"/readme", 7, kReadmeFile, kReadmeSize},
};

}  // namespace

bool Lookup(const char* name, unsigned long length, File& out) {
    for (const auto& entry : kEntries) {
        if (length != entry.name_length) {
            continue;
        }
        bool same = true;
        for (unsigned long index = 0; index < length; ++index) {
            if (name[index] != entry.name[index]) {
                same = false;
                break;
            }
        }
        if (same) {
            out = File{entry.data, entry.size, false};
            return true;
        }
    }
    out = Scratch();
    return false;
}

const File& Scratch() {
    static File scratch{g_scratch, 0, true};
    scratch.size = g_scratch_len;
    return scratch;
}

void ScratchGrew(unsigned long size) {
    if (size > g_scratch_len) {
        g_scratch_len = size;
    }
}

}  // namespace cinux::armv7::ramfs
