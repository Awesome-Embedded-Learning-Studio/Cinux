/**
 * @file    ramfs.hpp
 * @brief   The few files this port has without a filesystem.
 *
 * Two read-only files by name and one writable scratch buffer. Enough for
 * open, read, write and lseek to walk real bytes; when a block device
 * arrives this header is where the narrower interface should grow.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::armv7::ramfs {

/// One file as the rest of the kernel sees it.
struct File {
    const unsigned char* data;      ///< Contents.
    unsigned long        size;      ///< How many bytes are valid.
    bool                 writable;  ///< Reads are always allowed.
};

/// Finds a file by name; false when the name is unknown.
bool Lookup(const char* name, unsigned long length, File& out);

/// The writable buffer, for the scratch file.
const File& Scratch();

/// Records how many bytes of the scratch file are valid.
void ScratchGrew(unsigned long size);

}  // namespace cinux::armv7::ramfs
