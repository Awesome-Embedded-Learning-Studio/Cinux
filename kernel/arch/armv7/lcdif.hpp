/**
 * @file    lcdif.hpp
 * @brief   The i.MX6UL LCDIF, made to show one framebuffer.
 *
 * QEMU's mcimx6ul-evk models the LCDIF with a deliberately small
 * register face: a transfer count, a control word whose RUN and
 * WORD_LENGTH bits matter, and the next-buffer address whose write is
 * the whole frame trigger. This driver programs exactly that face —
 * one mode (XRGB8888), one buffer, no interrupts — and hands the
 * buffer's kernel address to whoever wants to draw. The larger panel
 * timing and the alpha plane stay unwired until a consumer needs them.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::armv7::lcdif {

/// Panel this bring-up drives: small enough for one order-8 block.
constexpr unsigned long kWidth  = 320;
constexpr unsigned long kHeight = 240;

/// Reports whether the controller answers at its register window.
[[nodiscard]] bool Probe();

/**
 * @brief         Brings the panel up with one writable framebuffer.
 * @return        The kernel-visible base of the XRGB8888 image, or 0
 *                when the controller is absent or the pages ran out.
 * @note          Bring-up wiring, single-threaded, before any drawer.
 * @since         0.1.0
 * @ingroup kernel_arch
 */
[[nodiscard]] unsigned long InitFramebuffer();

/// The framebuffer from InitFramebuffer, 0 before it succeeds.
[[nodiscard]] unsigned long Framebuffer();

}  // namespace cinux::armv7::lcdif
