/**
 * @file    root_port.cpp
 * @brief    The x86_64 half of the root volume port.
 *
 * @author  Charliechen114514
 * @date    2026-11-15
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch_x86_64
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/root_port.hpp"

#include "kernel/drivers/ahci/ahci.hpp"
#include "kernel/drivers/block/block_device_concept.hpp"
#include "kernel/drivers/block/mbr_window.hpp"
#include "kernel/drivers/rtc/rtc.hpp"

namespace cinux::arch {

namespace {

unsigned long long rtc_wall_clock() {
    return driver::Rtc::self().now_seconds();
}

}  // namespace

driver::BlockPort* MakeRootPort(unsigned long long (**wall_clock)()) {
    if (wall_clock != nullptr) {
        *wall_clock = rtc_wall_clock;
    }
    // The same SATA seat carries either disk shape: a card whose volume
    // lives inside an MBR Linux partition, or a bare ext2 volume that is
    // the whole disk. The scan decides, so both shapes mount through
    // this one port with no caller-side switch.
    static driver::MbrWindow root_window;
    if (root_window.adopt(driver::Ahci::self())) {
        return root_window.port();
    }
    static driver::BlockPort sata_port;
    sata_port.adopt(driver::Ahci::self());
    return &sata_port;
}

}  // namespace cinux::arch
