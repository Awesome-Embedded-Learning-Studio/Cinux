/**
 * @file    lcdif.cpp
 * @brief   The i.MX6UL LCDIF driver body: probe, one mode, one buffer.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/lcdif.hpp"

#include <cstdint>

#include "kernel/mm/layout.hpp"
#include "kernel/mm/hardware_frames.hpp"
#include "kernel/mm/pmm.hpp"

namespace cinux::armv7::lcdif {

namespace {

/// The controller's register window on this SoC.
constexpr unsigned long kBase = 0x021C8000UL;

/// Register offsets the QEMU model answers; stride is sixteen bytes.
constexpr unsigned long kCtrl           = 0x00;
constexpr unsigned long kTransferCount  = 0x30;
constexpr unsigned long kNextBuffer     = 0x50;

/// CTRL bits: RUN at 0, WORD_LENGTH at bit 8; value 3 means 24-bit
/// words, which the model reads as XRGB8888.
constexpr unsigned int kCtrlRun          = 1U << 0;
constexpr unsigned int kCtrlWordXrgb8888 = 3U << 8;

/// Bytes per pixel this driver programs.
constexpr unsigned long kBytesPerPixel = 4;

/// Pages one frame needs, rounded to the power-of-two allocation.
constexpr int kFrameOrder = 9;  // 512 pages, 2 MiB: fits 300 KiB with room

unsigned long g_framebuffer = 0;

/// One 32-bit register of the controller.
inline volatile unsigned int* Reg(unsigned long offset) {
    return reinterpret_cast<volatile unsigned int*>(kBase + offset);
}

}  // namespace

bool Probe() {
    // A kept write is the honest witness: an unimplemented stub reads
    // zero back, the model keeps what it was given.
    auto* const kProbeWord = Reg(kTransferCount);
    const unsigned int kKeep = *kProbeWord;
    *kProbeWord = 0x5AC3A5A5U;
    const unsigned int kBack = *kProbeWord;
    *kProbeWord = kKeep;
    return kBack == 0x5AC3A5A5U;
}

unsigned long InitFramebuffer() {
    if (g_framebuffer != 0) {
        return g_framebuffer;
    }
    if (!Probe()) {
        return 0;
    }
    const base::PhysAddr kPages = mm::Pmm::self().allocate_pages(kFrameOrder);
    if (kPages == base::PhysAddr{}) {
        return 0;
    }
    const unsigned long kFramePhys = kPages.raw;
    // The panel's frame belongs to the screen from here on: address
    // spaces may map it, and their teardown must not hand it back to
    // the allocator while the LCDIF still scans it out.
    g_framebuffer = cinux::arch::space::DirectMapVirt(kFramePhys);

    // A first image worth showing: a green field with a red bar at the
    // top, so a screenshot tells the panel from a black console.
    auto* const kPixels = reinterpret_cast<unsigned int*>(g_framebuffer);
    for (unsigned long row = 0; row < kHeight; ++row) {
        for (unsigned long column = 0; column < kWidth; ++column) {
            kPixels[row * kWidth + column] =
                row < 16 ? 0xFF0000FFU : 0xFF00AA00U;
        }
    // A marker a program can read through its own frame mapping: seeing
    // this word proves the mapping and the panel are the same memory.
    kPixels[((kHeight - 4UL) * kWidth) + 5UL] = 0xABCD1234U;
    }

    // Program the face the model answers, in the order it needs: the
    // geometry first, then the mode and RUN, and the frame trigger last.
    *Reg(kTransferCount) =
        static_cast<unsigned int>(kWidth | (kHeight << 16));
    *Reg(kCtrl) = kCtrlRun | kCtrlWordXrgb8888;
    *Reg(kNextBuffer) = static_cast<unsigned int>(kFramePhys);
    return g_framebuffer;
}

unsigned long Framebuffer() {
    return g_framebuffer;
}

}  // namespace cinux::armv7::lcdif
