/**
 * @file    sys_stat_record.cpp
 * @brief   The stat record writer and the descriptor-shaped stat: the
 *          two pieces of the metadata face that touch no path.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.3.0
 * @ingroup kernel_syscall
 * @copyright Copyright (c) 2026
 */

#include "kernel/syscall/sys_stat.hpp"

#include <cstdint>

#include "cinux/result.hpp"
#include "cinux/shared.hpp"
#include "kernel/arch/abi.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/syscall/syscall_internal.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::syscall {

namespace {

/// Writes one little-endian field at a byte offset. Records differ in
/// width per ABI, so the fill is field by field rather than a struct
/// shaped for one machine.
void PutField(unsigned char* sink, unsigned long offset, unsigned long long value,
              unsigned long width) {
    for (unsigned long index = 0; index < width; ++index) {
        sink[offset + index] = static_cast<unsigned char>((value >> (index * 8U)) & 0xFFU);
    }
}

}  // namespace

void WriteStatRecord(unsigned char* sink, const fs::FsStat& probed) {
    constexpr arch::abi::StatLayout kLayout = arch::abi::kStatLayout;
    for (unsigned long index = 0; index < kLayout.bytes; ++index) {
        sink[index] = 0;
    }
    PutField(sink, kLayout.dev_off, 0, kLayout.dev_size);
    PutField(sink, kLayout.ino_off, probed.ino, kLayout.ino_size);
    if (kLayout.ino_low_size != 0) {
        PutField(sink, kLayout.ino_low_off, probed.ino, kLayout.ino_low_size);
    }
    PutField(sink, kLayout.mode_off, probed.mode, kLayout.mode_size);
    PutField(sink, kLayout.links_off, probed.links, kLayout.links_size);
    PutField(sink, kLayout.rdev_off, probed.rdev, kLayout.rdev_size);
    PutField(sink, kLayout.size_off, probed.size, kLayout.size_size);
    PutField(sink, kLayout.blksize_off, 4096, kLayout.blksize_size);
    PutField(sink, kLayout.blocks_off, (probed.size + 511UL) / 512UL, kLayout.blocks_size);
    PutField(sink, kLayout.atime_off, probed.mtime, kLayout.time_size);
    PutField(sink, kLayout.mtime_off, probed.mtime, kLayout.time_size);
    PutField(sink, kLayout.ctime_off, probed.mtime, kLayout.time_size);
}

long long StatDescriptor(unsigned long long descriptor, unsigned long long destination) {
    if (!UserRangeWritable(destination, arch::abi::kStatLayout.bytes)) {
        return -kEfault;
    }
    const base::Result<base::Shared<fs::OpenFile>> kSlot = LiveSlot(descriptor);
    if (!kSlot.ok()) {
        return -kEbadf;
    }
    if (kSlot.value()->ops->stat == nullptr) {
        return -kEnosys;
    }
    fs::FsStat               probed{};
    const base::Result<void> kProbed =
        kSlot.value()->ops->stat(kSlot.value()->backend, kSlot.value()->node, probed);
    if (!kProbed.ok()) {
        return -ErrnoFrom(kProbed.error());
    }
    unsigned char record[arch::abi::kStatLayout.bytes] = {};
    WriteStatRecord(record, probed);
    if (!uaccess::CopyToUser(destination, record, sizeof(record))) {
        return -kEfault;
    }
    return 0;
}

}  // namespace cinux::syscall
