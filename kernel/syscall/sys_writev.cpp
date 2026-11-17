#include "kernel/syscall/sys_writev.hpp"

#include <utility>

#include "kernel/arch/abi.hpp"
#include "kernel/syscall/sys_write.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::syscall {

namespace {

/// One scatter segment read out of the caller's array. Its width and
/// field placement belong to the ABI, not to this file: a 32-bit port
/// writes eight-byte entries where a 64-bit one writes sixteen.
struct IoVector {
    unsigned long long base;
    unsigned long long bytes;
};

/// Reads one segment: the two fields land in the neutral pair whatever
/// width the ABI gives them.
bool ReadIoVector(unsigned long long at, IoVector& run) {
    unsigned char raw[cinux::arch::abi::kIovecBytes]{};
    if (!uaccess::CopyFromUser(raw, at, sizeof(raw))) {
        return false;
    }
    run.base  = 0;
    run.bytes = 0;
    for (unsigned long index = 0; index < cinux::arch::abi::kIovecBaseBytes; ++index) {
        run.base |= static_cast<unsigned long long>(raw[cinux::arch::abi::kIovecBaseOffset + index])
                    << (index * 8U);
    }
    for (unsigned long index = 0; index < cinux::arch::abi::kIovecLengthBytes; ++index) {
        run.bytes |=
            static_cast<unsigned long long>(raw[cinux::arch::abi::kIovecLengthOffset + index])
            << (index * 8U);
    }
    return true;
}

}  // namespace

long long HandleWritev(const SyscallFrame& frame) {
    const unsigned long long kSegments = frame.rdx;
    if (kSegments > 1024) {
        return -kEinval;
    }
    if (kSegments != 0 && frame.rsi >= kUserAddressLimit) {
        return -kEfault;
    }
    long long total = 0;
    for (unsigned long long index = 0; index < kSegments; ++index) {
        IoVector run{};
        if (!ReadIoVector(frame.rsi + (index * cinux::arch::abi::kIovecBytes), run)) {
            return total > 0 ? total : -kEfault;
        }
        const long long kPut = WriteRun(frame.rdi, run.base, run.bytes);
        if (kPut < 0) {
            return total > 0 ? total : kPut;
        }
        total += kPut;
        if (std::cmp_less(kPut, run.bytes)) {
            return total;
        }
    }
    return total;
}

}  // namespace cinux::syscall
