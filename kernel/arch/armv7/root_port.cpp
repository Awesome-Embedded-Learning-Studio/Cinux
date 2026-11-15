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
    static driver::BlockPort card_port;
    card_port.adopt(driver::UsdhcHost::self());
    if (wall_clock != nullptr) {
        *wall_clock = timer_wall_clock;
    }
    return &card_port;
}

}  // namespace cinux::arch
