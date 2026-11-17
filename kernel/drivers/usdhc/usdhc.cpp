/**
 * @file    usdhc.cpp
 * @brief   One SD card walked to life through the i.MX uSDHC door.
 *
 * The register layout is the controller's own, with two shapes the
 * model in the machine hands out: the transfer-mode word rides the
 * vendor MIX register and the command word fires from the upper half
 * of the shared frame, and the bus-width byte arrives in the vendor's
 * bit seats. Everything else answers at the standard seats.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#include "kernel/drivers/usdhc/usdhc.hpp"

#include <cstdint>

#include "cinux/memory.hpp"
#include "kernel/arch/instructions.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/boot/print.hpp"

namespace cinux::driver {

namespace {

/// The command frame's offset: low half transfer mode, high half fires.
constexpr unsigned long kRegBlock = 0x04;
constexpr unsigned long kRegArg = 0x08;
constexpr unsigned long kRegCmd = 0x0C;
constexpr unsigned long kRegRsp0 = 0x10;
constexpr unsigned long kRegRsp3 = 0x1C;
constexpr unsigned long kRegData = 0x20;
constexpr unsigned long kRegState = 0x24;
constexpr unsigned long kRegBus = 0x28;
constexpr unsigned long kRegClock = 0x2C;
constexpr unsigned long kRegIntStatus = 0x30;
constexpr unsigned long kRegIntEnable = 0x34;
constexpr unsigned long kRegMix = 0x48;

/// Present state: a card sits in the socket.
constexpr std::uint32_t kCardPresent = 1U << 16U;

/// Clock control: ask, then wait, then let the bus run.
constexpr std::uint32_t kClockInternal = 1U << 0U;
constexpr std::uint32_t kClockStable = 1U << 1U;
constexpr std::uint32_t kClockBus = 1U << 2U;

/// Interrupt status bits the driver parks on.
constexpr std::uint32_t kIntCommand = 1U << 0U;
constexpr std::uint32_t kIntTransfer = 1U << 1U;
constexpr std::uint32_t kIntWriteReady = 1U << 4U;
constexpr std::uint32_t kIntReadReady = 1U << 5U;
constexpr std::uint32_t kIntError = 1U << 15U;

/// Command word seats, in the frame's upper half.
constexpr unsigned kRespLen136 = 1U;
constexpr unsigned kRespLen48 = 2U;
constexpr unsigned kRespLen48Busy = 3U;
constexpr unsigned kCheckCrc = 1U << 3U;
constexpr unsigned kCheckIndex = 1U << 4U;
constexpr unsigned kDataPresent = 1U << 5U;

/// Transfer mode seats, cached through the vendor MIX register.
constexpr std::uint32_t kMixRead = 1U << 4U;

/// The vendor's four-bit bus seat inside the bus byte.
constexpr std::uint32_t kBusFourBit = 0x1U << 1U;

/// Bytes in one card sector.
constexpr unsigned long kSectorBytes = 512;

/// Bounded waits: the model answers inside a breath, the bound only
/// guards a dead machine.
constexpr unsigned long kSpinLimit = 20000000U;

/// One idle beat between waits: the machine's own workers need the core.
constexpr void beat() {
    arch::CpuIdle();
}

}  // namespace

bool UsdhcHost::init(const unsigned long base_physical) {
    regs_ = reinterpret_cast<volatile std::uint32_t*>(  // NOLINT(performance-no-int-to-ptr)
        cinux::arch::space::DirectMapVirt(base_physical));

    // A card must sit in the socket before any word of the dance.
    if ((regs_[kRegState / 4U] & kCardPresent) == 0) {
        fails_ = 1;
        return false;
    }

    // The clock door: ask for the internal clock, wait for it to
    // settle, then let the bus run.
    regs_[kRegClock / 4U] = kClockInternal;
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        if ((regs_[kRegClock / 4U] & kClockStable) != 0) {
            break;
        }
        beat();
    }
    if ((regs_[kRegClock / 4U] & kClockStable) == 0) {
        fails_ = 2;
        return false;
    }
    regs_[kRegClock / 4U] = kClockInternal | kClockBus;

    // Status latching on, signal line quiet: the driver polls.
    regs_[kRegIntEnable / 4U] =
        kIntCommand | kIntTransfer | kIntWriteReady | kIntReadReady;

    // The card's own walk: idle, ask the voltage window, take a name.
    unsigned response = 0;
    if (!command(0U, 0U, 0U, response)) {
        return false;
    }
    // CMD8 asks whether the card speaks the second dialect; a refusal
    // is an answer too, the older cards simply stay quiet.
    (void)command(8U, 0x1AAU, kRespLen48 | kCheckCrc | kCheckIndex, response);
    for (unsigned long try_index = 0; try_index < 2000U; ++try_index) {
        if (!command(55U, 0U, kRespLen48 | kCheckCrc | kCheckIndex, response)) {
            return false;
        }
        if (!command(41U, 0x40ff8000U, kRespLen48, response)) {
            return false;
        }
        if ((response & (1U << 31U)) != 0) {
            break;
        }
        beat();
    }
    if ((response & (1U << 31U)) == 0) {
        fails_ = 4;
        return false;
    }
    // CMD2 collects the card's name; CMD3 hands back its address.
    unsigned long_response[4] = {};
    if (!command(2U, 0U, kRespLen136 | kCheckCrc, response, long_response)) {
        return false;
    }
    if (!command(3U, 0U, kRespLen48 | kCheckCrc | kCheckIndex, response)) {
        return false;
    }
    rca_ = response >> 16U;
    if (rca_ == 0) {
        fails_ = 5;
        return false;
    }
    // CMD9 returns the card's shape: the second dialect keeps its
    // capacity word whole inside the second response word.
    unsigned shape[4] = {};
    if (!command(9U, rca_ << 16U, kRespLen136 | kCheckCrc, response, shape)) {
        return false;
    }
    // The card's shape word rebuilds the CSD byte stream the standard
    // way: the last response word opens it, each earlier word hands the
    // next four bytes. Both dialects keep their capacity word inside.
    std::uint8_t csd[16] = {};
    csd[0] = shape[3] >> 16U;
    csd[1] = shape[3] >> 8U;
    csd[2] = shape[3];
    for (unsigned index = 0; index < 4U; ++index) {
        csd[3U + index] = shape[2] >> (24U - 8U * index);
        csd[7U + index] = shape[1] >> (24U - 8U * index);
        csd[11U + index] = shape[0] >> (24U - 8U * index);
    }
    byte_addressed_ = (csd[0] >> 6U) == 0U;
    if ((csd[0] >> 6U) != 0U) {
        // The second dialect: one capacity word, half-megabyte units.
        const std::uint32_t kCapacity =
            ((csd[7] & 0x3FU) << 16U) | (csd[8] << 8U) | csd[9];
        sectors_ = (static_cast<std::uint64_t>(kCapacity) + 1U) * 1024U;
    } else {
        // The first dialect: three size words and a block length.
        const std::uint32_t kBlocks =
            ((csd[6] & 0x3U) << 10U) | (csd[7] << 2U) | (csd[8] >> 6U);
        const unsigned kMult = ((csd[9] & 0x3U) << 1U) | (csd[10] >> 7U);
        const unsigned kReadLen = csd[5] & 0xFU;
        sectors_ = (static_cast<std::uint64_t>(kBlocks) + 1U)
                   << (kMult + 2U + kReadLen - 9U);
    }
    // CMD7 selects the card; the pair after it widens the bus.
    if (!command(7U, rca_ << 16U, kRespLen48Busy | kCheckCrc | kCheckIndex, response)) {
        return false;
    }
    if (!command(55U, rca_ << 16U, kRespLen48 | kCheckCrc | kCheckIndex, response)) {
        return false;
    }
    if (!command(6U, 2U, kRespLen48 | kCheckCrc | kCheckIndex, response)) {
        return false;
    }
    regs_[kRegBus / 4U] = kBusFourBit;
    return true;
}

bool UsdhcHost::command(const unsigned index, const unsigned argument, const unsigned flags,
                        unsigned& response, unsigned* long_response) {
    // Only one command may ride the wire at a time.
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        if ((regs_[kRegState / 4U] & 0x1U) == 0) {
            break;
        }
        beat();
    }
    if ((regs_[kRegState / 4U] & 0x1U) != 0) {
        fails_ = 6;
        return false;
    }

    regs_[kRegArg / 4U] = argument;
    const std::uint32_t kWord = (static_cast<std::uint32_t>(index) << 8U) | flags;
    regs_[kRegCmd / 4U] = kWord << 16U;

    // Poll the command door; errors park the wait early.
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        const std::uint32_t kStatus = regs_[kRegIntStatus / 4U];
        if ((kStatus & (kIntCommand | kIntError)) != 0) {
            regs_[kRegIntStatus / 4U] = kStatus & (kIntCommand | kIntError);
            if ((kStatus & kIntError) != 0) {
                fails_ = 7;
                return false;
            }
            break;
        }
        beat();
    }
    if ((regs_[kRegIntStatus / 4U] & kIntError) != 0) {
        fails_ = 7;
        return false;
    }

    response = regs_[kRegRsp0 / 4U];
    if (long_response != nullptr) {
        long_response[0] = regs_[kRegRsp0 / 4U];
        long_response[1] = regs_[(kRegRsp0 / 4U) + 1U];
        long_response[2] = regs_[(kRegRsp0 / 4U) + 2U];
        long_response[3] = regs_[kRegRsp3 / 4U];
    }
    return true;
}

bool UsdhcHost::move_sector(const unsigned long sector, void* page, const bool reading) {
    regs_[kRegBlock / 4U] = 512U | (1U << 16U);
    regs_[kRegMix / 4U] = reading ? kMixRead : 0U;
    // The first dialect counts bytes, the second counts blocks; the
    // card's dialect decides how far one sector's argument reaches.
    regs_[kRegArg / 4U] = static_cast<std::uint32_t>(
        byte_addressed_ ? sector * kSectorBytes : sector);
    const unsigned kFlags = kRespLen48 | kCheckCrc | kCheckIndex | kDataPresent;
    const unsigned kIndex = reading ? 17U : 24U;
    regs_[kRegCmd / 4U] = ((static_cast<std::uint32_t>(kIndex) << 8U) | kFlags) << 16U;

    // The buffer door first, then the transfer's own close.
    const std::uint32_t kReady = reading ? kIntReadReady : kIntWriteReady;
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        const std::uint32_t kStatus = regs_[kRegIntStatus / 4U];
        if ((kStatus & (kReady | kIntError)) != 0) {
            regs_[kRegIntStatus / 4U] = kStatus & (kReady | kIntError);
            if ((kStatus & kIntError) != 0) {
                fails_ = 8;
                return false;
            }
            break;
        }
        beat();
    }
    // Command's own close word still waits.
    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        const std::uint32_t kStatus = regs_[kRegIntStatus / 4U];
        if ((kStatus & kIntCommand) != 0) {
            regs_[kRegIntStatus / 4U] = kStatus & kIntCommand;
            break;
        }
        beat();
    }

    auto* const kWords = static_cast<volatile std::uint32_t*>(page);
    if (reading) {
        for (unsigned index = 0; index < kSectorBytes / 4U; ++index) {
            kWords[index] = regs_[kRegData / 4U];
        }
    } else {
        auto* const kSource = static_cast<const std::uint32_t*>(page);
        for (unsigned index = 0; index < kSectorBytes / 4U; ++index) {
            regs_[kRegData / 4U] = kSource[index];
        }
    }
    arch::MemoryBarrier();

    for (unsigned long spin = 0; spin < kSpinLimit; ++spin) {
        const std::uint32_t kStatus = regs_[kRegIntStatus / 4U];
        if ((kStatus & (kIntTransfer | kIntError)) != 0) {
            regs_[kRegIntStatus / 4U] = kStatus & (kIntTransfer | kIntError);
            if ((kStatus & kIntError) != 0) {
                fails_ = 9;
                return false;
            }
            break;
        }
        beat();
    }
    return true;
}

bool UsdhcHost::read_blocks(const Lba first, const BlockSpan span, std::uint8_t* destination) {
    for (std::uint16_t index = 0; index < span.value; ++index) {
        if (first.value + index >= sectors_) {
            fails_ = 10;
            return false;
        }
        if (!move_sector(static_cast<unsigned long>(first.value + index),
                         destination + (static_cast<unsigned long>(index) * kSectorBytes),
                         true)) {
            return false;
        }
    }
    return true;
}

bool UsdhcHost::write_blocks(const Lba first, const BlockSpan span, const std::uint8_t* source) {
    for (std::uint16_t index = 0; index < span.value; ++index) {
        if (first.value + index >= sectors_) {
            fails_ = 10;
            return false;
        }
        if (!move_sector(static_cast<unsigned long>(first.value + index),
                         const_cast<std::uint8_t*>(source) +
                             (static_cast<unsigned long>(index) * kSectorBytes),
                         false)) {
            return false;
        }
    }
    return true;
}

std::uint64_t UsdhcHost::block_count() {
    return sectors_;
}

unsigned long UsdhcHost::block_size() {
    return kSectorBytes;
}

bool UsdhcHost::flush() {
    return true;
}

unsigned long UsdhcHost::last_fail() const {
    return fails_;
}

}  // namespace cinux::driver
