/**
 * @file    mbr_window.hpp
 * @brief   The one MBR reader: seats a window over the disk's Linux
 *          partition, or reports the disk carries none.
 *
 * Both lanes meet their volume through this class. A card baked by the
 * layout tables holds an MBR whose first Linux partition (type 0x83)
 * frames the ext2 volume; the scan reads sector zero once, and on
 * success hands back a BlockPort over the partition, so the ladder
 * above never learns the disk is sliced. A disk without a usable
 * partition table returns false and stays a whole-disk volume -- the
 * bare ext2 images boot through exactly that path.
 *
 * @author  Charliechen114514
 * @date    2026-11-17
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#pragma once

#include "kernel/drivers/block/block_device_concept.hpp"

namespace cinux::driver {

/**
 * @brief   One whole-disk device seen through its MBR's Linux partition.
 * @note    The instance owns the 512-byte sector buffer, the window, and
 *          the port over it, so callers park it in static storage and
 *          the port stays valid for the system's lifetime. adopt() is
 *          idempotent: a second call returns the first answer without
 *          touching the disk again.
 * @since   0.1.0
 * @ingroup kernel_driver
 */
class MbrWindow {
public:
    /**
     * @brief         Reads the device's sector zero and seats the window.
     *
     * @param[in,out] device  The whole-disk device instance.
     *
     * @return        True when sector zero carries an MBR signature and
     *                      a Linux partition with a nonzero start; the
     *                      port() then names that partition. False when
     *                      the read failed or no usable entry exists,
     *                      leaving the disk to be used whole.
     * @since         0.1.0
     * @ingroup       kernel_driver
     */
    template <BlockDevice Dev>
    bool adopt(Dev& device) {
        if (seated_) {
            return true;
        }
        BlockPort whole;
        whole.adopt(device);
        if (!whole.read_blocks(Lba{0}, BlockSpan{1}, mbr_)) {
            return false;
        }
        if (mbr_[510] != 0x55 || mbr_[511] != 0xAA) {
            return false;
        }
        for (unsigned entry = 0; entry < 4; ++entry) {
            const unsigned char* row = mbr_ + 446 + 16 * entry;
            if (row[4] != 0x83) {
                continue;
            }
            const std::uint64_t kStart =
                static_cast<std::uint64_t>(row[8]) |
                (static_cast<std::uint64_t>(row[9]) << 8U) |
                (static_cast<std::uint64_t>(row[10]) << 16U) |
                (static_cast<std::uint64_t>(row[11]) << 24U);
            if (kStart == 0) {
                continue;
            }
            window_.adopt(device, kStart);
            port_.adopt(window_);
            base_   = kStart;
            seated_ = true;
            return true;
        }
        return false;
    }

    /**
     * @brief         The partition's port, valid after a true adopt().
     *
     * @return        The windowed port, or nullptr before adoption.
     * @since         0.1.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] BlockPort* port() {
        return seated_ ? &port_ : nullptr;
    }

    /**
     * @brief         The seated partition's first sector on the disk.
     *
     * @return        Partition base in sectors, zero before adoption.
     * @since         0.1.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] std::uint64_t base() const {
        return base_;
    }

private:
    BlockWindow   window_;
    BlockPort     port_;
    unsigned char mbr_[512] = {};
    std::uint64_t base_     = 0;
    bool          seated_   = false;
};

}  // namespace cinux::driver
