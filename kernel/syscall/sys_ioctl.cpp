#include "kernel/syscall/sys_ioctl.hpp"

#include <cstdint>

#include "cinux/bit_ops/bitmask.hpp"
#include "cinux/byte_order.hpp"
#include "cinux/container/name_table.hpp"
#include "cinux/memory.hpp"
#include "cinux/result.hpp"
#include "cinux/shared.hpp"
#include "kernel/drivers/framebuffer/framebuffer_config.hpp"
#include "kernel/drivers/input/input_config.hpp"
#include "kernel/drivers/tty/tty_config.hpp"
#include "kernel/fs/open_file.hpp"
#include "kernel/syscall/syscall.hpp"
#include "kernel/syscall/syscall_internal.hpp"
#include "kernel/syscall/uaccess.hpp"

namespace cinux::syscall {

namespace {

/// One pointer-carrying command's span: how many bytes, and whether
/// the caller hands them over or takes them back. Zero bytes marks the
/// wordless commands, whose argument is a flag, not an address.
struct WordSpan {
    unsigned long long word;
    unsigned long      bytes;
    bool               caller_gives;
};

const WordSpan kWordSpans[] = {
    {.word = driver::kTtyTcgets2, .bytes = driver::kTermios2Bytes, .caller_gives = false},
    {.word = driver::kTtyTcsets2, .bytes = driver::kTermios2Bytes, .caller_gives = true},
    {.word = driver::kTtyTcsetsw2, .bytes = driver::kTermios2Bytes, .caller_gives = true},
    {.word = driver::kTtyTcsetsf2, .bytes = driver::kTermios2Bytes, .caller_gives = true},
    {.word = driver::kTtyTcgets, .bytes = driver::kTermiosBytes, .caller_gives = false},
    {.word = driver::kTtyTcsets, .bytes = driver::kTermiosBytes, .caller_gives = true},
    {.word = driver::kTtyTcsetsw, .bytes = driver::kTermiosBytes, .caller_gives = true},
    {.word = driver::kTtyTcsetsf, .bytes = driver::kTermiosBytes, .caller_gives = true},
    {.word = driver::kTtyTiocsctty, .bytes = 0, .caller_gives = true},
    {.word = driver::kTtyTiocgpgrp, .bytes = 4, .caller_gives = false},
    {.word = driver::kTtyTiocspgrp, .bytes = 4, .caller_gives = true},
    {.word = driver::kTtyTiocgwinsz, .bytes = 8, .caller_gives = false},
    {.word = driver::kTtyTiocswinsz, .bytes = 8, .caller_gives = true},
    {.word = driver::kTtyTiocsptlck, .bytes = 4, .caller_gives = true},
    {.word = driver::kTtyTiocgptn, .bytes = 4, .caller_gives = false},
    {
        .word         = driver::kFbGetVarScreeninfo,
        .bytes        = driver::kFbVarScreeninfoBytes,
        .caller_gives = false,
    },
    {
        .word         = driver::kFbGetFixScreeninfo,
        .bytes        = driver::kFbFixScreeninfoBytes,
        .caller_gives = false,
    },
    {.word = driver::kEvdevGetAbsX, .bytes = sizeof(driver::InputAbsInfo), .caller_gives = false},
    {.word = driver::kEvdevGetAbsY, .bytes = sizeof(driver::InputAbsInfo), .caller_gives = false},
    {.word = driver::kEvdevGrab, .bytes = 0, .caller_gives = true},
};

/// The widest argument any known word carries, the stage's size.
constexpr unsigned long kStageBytes = driver::kFbVarScreeninfoBytes;

}  // namespace

long long HandleIoctl(const SyscallFrame& frame) {
    const base::Result<base::Shared<fs::OpenFile>> kSlot = LiveSlot(frame.rdi);
    if (!kSlot.ok()) {
        return -kEbadf;
    }
    fs::OpenFile* const          kFile    = kSlot.value().get();
    constexpr unsigned long long kFionbio = 0x5421;
    if (frame.rsi == kFionbio) {
        int enabled = 0;
        if (!uaccess::CopyFromUser(&enabled, frame.rdx, sizeof(enabled))) {
            return -kEfault;
        }
        if (enabled != 0) {
            kFile->status_flags |= kOpenNonblock;
        } else {
            kFile->status_flags &= ~kOpenNonblock;
        }
        return 0;
    }
    if (kFile->ops->ioctl == nullptr) {
        return -kEnotty;
    }
    WordSpan        dynamic{};
    const WordSpan* span = base::container::FindByWord(kWordSpans, frame.rsi);
    if ((frame.rsi & driver::kEvdevGetKeysMask) == driver::kEvdevGetKeys) {
        const auto kBytes = base::bit::BitMask{frame.rsi}.extract(driver::kEvdevGetKeysSize);
        const auto kCap   = driver::InputKeyCodeBytes();
        dynamic           = {
            .word         = frame.rsi,
            .bytes        = kBytes < kCap ? kBytes : kCap,
            .caller_gives = false,
        };
        span = &dynamic;
    }
    // A word without a span keeps its raw argument: backends that
    // speak whole structures to the caller themselves (the socket
    // interface questions) own their user copies. Backends that only
    // know staged words answer -kEnotty for anything else.
    if (span == nullptr || span->bytes == 0) {
        return kFile->ops->ioctl(kFile->backend, kFile->node, frame.rsi, frame.rdx);
    }
    unsigned char stage[kStageBytes];
    cinux::base::SetBytes(stage, 0, sizeof(stage));
    if (span->caller_gives && !uaccess::CopyFromUser(stage, frame.rdx, span->bytes)) {
        return -kEfault;
    }
    const auto      kStageWord = reinterpret_cast<unsigned long long>(stage);
    const long long kAnswer = kFile->ops->ioctl(kFile->backend, kFile->node, frame.rsi, kStageWord);
    if (kAnswer >= 0 && !span->caller_gives &&
        !uaccess::CopyToUser(frame.rdx, stage, span->bytes)) {
        return -kEfault;
    }
    return kAnswer;
}

}  // namespace cinux::syscall
