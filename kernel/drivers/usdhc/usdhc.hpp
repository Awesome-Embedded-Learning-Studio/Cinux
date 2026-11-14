/**
 * @file    usdhc.hpp
 * @brief   The i.MX uSDHC seat: one SD card behind the neutral block face.
 *
 * @author  Charliechen114514
 * @date    2026-11-14
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_driver
 * @copyright Copyright (c) 2026
 */

#pragma once

#include <cstdint>

#include "cinux/singleton.hpp"
#include "kernel/drivers/block/block_device_concept.hpp"

namespace cinux::driver {

/**
 * @brief   One uSDHC controller's card, walked to life in single-block PIO.
 * @note    The card init dance (idle, voltage, RCA, CSD, select, four-bit
 *          bus) and the sector movers all live here; the five block
 *          methods satisfy the neutral BlockDevice contract, so the lane
 *          adopts the card through the same face the AHCI drive uses.
 * @since   0.1.0
 * @ingroup kernel_driver
 */
class UsdhcHost : public cinux::base::Singleton<UsdhcHost> {
    friend class cinux::base::Singleton<UsdhcHost>;

public:
    /**
     * @brief         Walks one controller's card to life.
     * @param[in]     base_physical   The controller's first register frame.
     * @return        True when a card answered and its shape is known.
     * @note          Bring-up wiring, once, before any block call.
     * @since         0.1.0
     * @ingroup       kernel_driver
     */
    [[nodiscard]] bool init(unsigned long base_physical);

    /// Reads span blocks at first into destination; blocks until done.
    [[nodiscard]] bool read_blocks(Lba first, BlockSpan span, std::uint8_t* destination);

    /// Writes span blocks at first from source; blocks until done.
    [[nodiscard]] bool write_blocks(Lba first, BlockSpan span, const std::uint8_t* source);

    /// How many 512-byte sectors the card addresses.
    [[nodiscard]] std::uint64_t block_count();

    /// The card's transfer unit in bytes.
    [[nodiscard]] unsigned long block_size();

    /// Orders writes behind this call before any later one.
    [[nodiscard]] bool flush();

    /// The last refusal's reason word, zero when nothing failed yet.
    [[nodiscard]] unsigned long last_fail() const;

private:
    /// Sends one command and waits for its response word.
    [[nodiscard]] bool command(unsigned index, unsigned argument, unsigned flags,
                               unsigned& response, unsigned* long_response = nullptr);

    /// Moves one sector between the card and a 512-byte page.
    [[nodiscard]] bool move_sector(unsigned long sector, void* page, bool reading);

    volatile std::uint32_t* regs_ = nullptr;
    std::uint32_t           rca_  = 0;
    bool                    byte_addressed_ = false;
    std::uint64_t           sectors_ = 0;
    unsigned long           fails_   = 0;
};

static_assert(BlockDevice<UsdhcHost>);

}  // namespace cinux::driver
