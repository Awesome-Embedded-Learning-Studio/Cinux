/**
 * @file    root_port.cpp
 * @brief    The armv7 half of the root volume port.
 *
 * The card behind the usdhc host is this lane's root volume; the
 * stamps read the timer's own counter scaled to seconds, which is all
 * a card-mounted volume needs on this board.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch_armv7
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/root_port.hpp"

#include "cinux/assert.hpp"

#include "kernel/arch/armv7/timer.hpp"
#include "kernel/drivers/block/block_device_concept.hpp"
#include "kernel/drivers/usdhc/usdhc.hpp"

namespace cinux::arch {

namespace {

unsigned long long timer_wall_clock() {
    const std::uint64_t kFrequency = cinux::armv7::TimerFrequency();
    if (kFrequency == 0) {
        return 0;
    }
    return cinux::armv7::TimerCounter() / kFrequency;
}

}  // namespace

driver::BlockPort* MakeRootPort(unsigned long long (**wall_clock)()) {
    // The card carries an MBR with one Linux partition; the volume
    // lives inside it, so the port the ladder climbs is a window over
    // that partition, not the raw card.
    static driver::BlockPort whole_card;
    whole_card.adopt(driver::UsdhcHost::self());
    static unsigned char mbr[512] = {};
    static driver::BlockWindow window;
    static driver::BlockPort card_port;
    static bool window_seated = false;
    if (!window_seated) {
        window_seated = true;
        if (!whole_card.read_blocks(driver::Lba{0}, driver::BlockSpan{1}, mbr)) {
            base::safety::Check(false, "root port could not read the card's MBR");
            return nullptr;
        }
        const bool kSignature = mbr[510] == 0x55 && mbr[511] == 0xAA;
        base::safety::Check(kSignature, "root port found no MBR signature");
        const bool kLinux = mbr[450] == 0x83;
        base::safety::Check(kLinux, "root port found no Linux partition");
        const std::uint64_t kStart = static_cast<std::uint64_t>(mbr[454]) |
                                     (static_cast<std::uint64_t>(mbr[455]) << 8U) |
                                     (static_cast<std::uint64_t>(mbr[456]) << 16U) |
                                     (static_cast<std::uint64_t>(mbr[457]) << 24U);
        base::safety::Check(kStart != 0, "root partition starts at sector zero");
        window.adopt(driver::UsdhcHost::self(), kStart);
        card_port.adopt(window);
    }
    if (wall_clock != nullptr) {
        *wall_clock = timer_wall_clock;
    }
    return &card_port;
}

}  // namespace cinux::arch
