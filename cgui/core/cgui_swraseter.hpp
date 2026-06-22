/**
 * @file cgui/core/cgui_swraseter.hpp
 * @brief cgui L3 software rasteriser -- pure-CPU pixel primitives (§4a skeleton)
 *
 * The render engine cgui core uses to draw into a staging Surface before
 * flushing it to the host. Everything is integer-only (Q8.8 blend) so it runs
 * on a CGUI_NO_FPU profile (MCU-F1) unchanged; no floats leak.
 *
 * §4a scope (see document/todo/f13-gui/cgui-02-refactor-and-separation.md §4):
 * this is the shape skeleton + unit tests. The primitives are generalised from
 * the legacy Canvas draw_* (canvas.cpp) but operate on a bare Surface descriptor
 * (no Canvas dependency) so cgui core stays host-agnostic. They are NOT wired
 * into pump yet -- pump still composites via wm.composite(); the
 * SwRaster takes over rendering in §4c (staging buffer + dirty region + flush).
 *
 * Pixel format: §4a implements XRGB8888 only (Desktop). Other formats
 * (ARGB8888 / RGB565 / 1BPP) assert-fail -- a format mismatch is an ABI
 * violation, not a silent fallback. They arrive with their profile support.
 *
 * Compile condition: CINUX_GUI.
 *
 * Namespace: cgui
 */
#pragma once

#include <stdint.h>

#include "cgui_host.h"  // cgui_pixel_format

namespace cinux::gui {

/**
 * @brief Half-open clip rectangle [x0,x1) x [y0,y1)
 *
 * Passed to every primitive; the operation is clipped to clip ∩ surface
 * bounds. nullptr means "clip to surface bounds only".
 */
struct ClipRect {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
};

/**
 * @brief Bare pixel-buffer descriptor -- SwRaster does NOT own the buffer
 *
 * The caller (pump staging buffer in §4c, or a host adapter / unit test)
 * owns the storage. stride_bytes is explicit (may exceed width*bpp/8 when the
 * backend aligns rows to a cache line -- presets §4 pixel-format hard contract).
 */
struct Surface {
    void*              pixels;
    uint32_t           width;
    uint32_t           height;
    uint32_t           stride_bytes;
    cgui_pixel_format format;
};

/**
 * @brief Solid rectangle fill (generalises Canvas::draw_rect)
 *
 * Pixels in [x,x+w) x [y,y+h) ∩ clip ∩ surface bounds are set to @p color.
 * Out-of-range regions are silently skipped.
 */
void fill_rect(Surface& s, int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color,
               const ClipRect* clip);

/**
 * @brief Opaque pixel copy src -> dst (generalises Canvas::blit)
 *
 * Copies the w x h region from (@p sx,@p sy) of @p src to (@p dx,@p dy) of
 * @p dst, clipped to dst bounds ∩ clip. Source columns/rows that would land
 * outside the clipped destination are skipped (the source offset tracks the
 * destination clip).
 */
void blit(Surface& dst, int32_t dx, int32_t dy, const Surface& src, uint32_t sx, uint32_t sy,
          uint32_t w, uint32_t h, const ClipRect* clip);

/**
 * @brief Q8.8 alpha blend src -> dst (integer-only, CGUI_NO_FPU safe)
 *
 * @p alpha_q8 in [0,256]: 0 = dst unchanged, 256 = fully src, 128 = 50/50.
 * Per channel: dst = (src*a + dst*(256-a)) >> 8. XRGB8888 only (alpha byte
 * ignored on input, written 0 on output).
 */
void blit_blend(Surface& dst, int32_t dx, int32_t dy, const Surface& src, uint32_t sx, uint32_t sy,
                uint32_t w, uint32_t h, uint16_t alpha_q8, const ClipRect* clip);

/**
 * @brief 1-bpp alpha-mask -> solid colour blit (glyph rendering)
 *
 * @p bits is a gh-row bitmap, MSB-first within each row, bytes_per_row =
 * (gw+7)/8 bytes per row. For each set bit the corresponding destination
 * pixel is set to @p color; clear bits are skipped (transparent). Generalises
 * the per-glyph loop in Canvas::draw_text.
 */
void glyph_blit(Surface& s, int32_t x, int32_t y, const uint8_t* bits, uint32_t gw, uint32_t gh,
                uint32_t color, const ClipRect* clip);

/**
 * @brief Bresenham line (generalises Canvas::draw_line), per-point clipped
 */
void draw_line(Surface& s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color,
               const ClipRect* clip);

}  // namespace cinux::gui
