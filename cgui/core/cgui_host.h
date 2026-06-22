/**
 * @file cgui/core/cgui_host.h
 * @brief cgui Host ABI -- the ONLY hard seam between cgui core and host
 *
 * DRAFT v2 ABI. cgui core never touches framebuffer / IRQ / syscall / process
 * structures directly -- it only calls through this table. Swapping host
 * (Cinux kernel / future user-space server / MCU bare-metal / SDL simulator)
 * = swapping the table fill. That is the "not aware of user vs kernel mode"
 * mechanism.
 *
 * v2 decisions (external review):
 *   - Split into CORE table (all hosts fill) + DESKTOP EXTENSION (spawn/rpc,
 *     NULL on MCU). MCU-forever-NULL entries do not belong in the one hard
 *     seam as first-class columns.
 *   - Display uses flush(area, pixels, stride, fmt) + flush_complete (NOT
 *     begin_frame->pointer), which is unsafe for STREAM-PAGE / SPI DMA /
 *     user-space shared buffer. flush_complete is host->core (backend notifies
 *     core), not a core->host call.
 *   - GPU is texture-compositor priority; no primitive CommandBuffer early.
 *
 * Freestanding C header.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cgui_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Pixel format (v2 hard contract: stride / endianness / premultiplied alpha
 * / byte-bit order -- aligned to Wayland shm rigour; see presets §4).
 * ============================================================ */
enum cgui_pixel_format {
    CGUI_PIX_XRGB8888 = 1, /* Desktop, 32bpp (no alpha) */
    CGUI_PIX_ARGB8888 = 2, /* 32bpp, premultiplied alpha */
    CGUI_PIX_RGB565   = 3, /* MCU-Color, 16bpp */
    CGUI_PIX_1BPP     = 4, /* MCU-F1 mono OLED, alpha-mask (NOT colorkey) */
};

/* ============================================================
 * Frame transfer types (host reports what changed; core flushes it).
 * ============================================================ */

/* Half-open dirty rect [x0,x1) x [y0,y1) in display coordinates. */
struct cgui_rect {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
};

/* One frame's host->core transfer. The CORE (pump) owns the rects buffer and
 * the flush loop; the HOST owns input dispatch + rendering and fills this in
 * render_frame():
 *   - rects/max_rects : core-allocated buffer (capacity in).
 *   - count           : host writes the number of dirty rects (0 = idle).
 *   - pixels/stride/w/h/fmt : host writes the staging buffer it rendered into.
 * The core flushes each rect [x0,x1) x [y0,y1) from pixels (stride row pitch)
 * to the display via flush(). count==0 means nothing changed -- core flushes
 * nothing (idle skip). */
struct cgui_frame {
    cgui_rect*        rects;
    uint32_t           max_rects;
    uint32_t           count;
    const void*        pixels;
    uint32_t           stride;
    uint32_t           width;
    uint32_t           height;
    cgui_pixel_format format;
};

/* ============================================================
 * Core host table -- every host fills this (MCU has no Desktop part).
 * ============================================================ */
struct cgui_host_core {
    /* ---- L1 Display backend: flush model (v2) ----
     * Core owns the staging/render buffer. After rendering it pushes each dirty
     * rect to the backend for display. Replaces begin_frame->pointer (unsafe
     * for STREAM-PAGE / SPI DMA / user-space shared buffer).
     *
     * Contract: @p pixels is the BASE of the staging buffer (not the rect's
     * top-left); @p x,@p y,@p w,@p h locate the dirty rect in display coords;
     * @p stride is the staging buffer's row stride in bytes (row r of the rect
     * is at pixels + (y+r)*stride + x*bpp). The backend copies that rect to the
     * display, honouring its own pitch. Multiple flush calls per frame are
     * possible (one per dirty rect); @p flush_complete signals when all are
     * pushed and the buffer is reusable.
     *
     * A Desktop host MUST provide flush: the pump composites into the staging
     * buffer and presents ONLY through this callback, so a NULL flush means
     * rendered frames never reach the display (the dirty region is still
     * consumed, so the display silently freezes until a new change). */
    void (*flush)(void* ctx, int x, int y, int w, int h, const void* pixels, uint32_t stride,
                  cgui_pixel_format fmt);
    void (*flush_complete)(void* ctx); /* host -> core: last async flush done, buffer reusable */
    /* power state (MCU sleep / display off) */
    void (*enter_sleep)(void* ctx);
    void (*exit_sleep)(void* ctx);

    /* ---- L2 Input backend ---- */
    bool (*poll_event)(void* ctx, cgui_event_header* out, uint16_t out_cap);

    /* ---- L4 Frame work (host owns input dispatch + rendering; core owns the
     *      frame loop + flush). cgui core never sees the host's GUI types --
     *      it drains raw events via poll_event, hands each to dispatch_event,
     *      then calls render_frame once and flushes the reported dirty rects.
     *      This is what keeps the core host-neutral (zero host includes). ---- */

    /* Consume one input event the core just drained. @p ev is the fixed-width
     * header; @p payload points to the ev->payload_len bytes immediately behind
     * it. The host deserialises + applies it to its own GUI state; any change
     * shows up later as dirty rects from render_frame. NULL = host has no input
     * path (events are dropped). */
    void (*dispatch_event)(void* ctx, const cgui_event_header* ev, const void* payload);

    /* Do all per-frame host work (poll outputs, cursor footprint, composite into
     * the staging buffer) and report the dirty rects + the staging buffer in
     * @p frame. frame->count == 0 means nothing changed this iteration (idle --
     * the core flushes nothing). Called once per pump iteration after the input
     * drain. NULL = host renders nothing (display frozen). */
    void (*render_frame)(void* ctx, cgui_frame* frame);

    /* ---- L2 Time backend ---- */
    uint32_t (*now_ms)(void* ctx);
    uint32_t (*next_deadline_ms)(void* ctx); /* for MCU __WFI throttling */

    /* ---- Memory / Log (all hosts have these) ---- */
    void* (*alloc)(void* ctx, size_t n);
    void (*free)(void* ctx, void* p);
    void (*log)(void* ctx, const char* fmt, ...);
};

/* ============================================================
 * Desktop extension -- Desktop profile only; NULL on MCU.
 * ============================================================ */
struct cgui_host_desktop {
    /* spawn a child process, returning its stdio handles (Desktop only). */
    int (*spawn)(void* ctx, const char* path, char* const argv[], int* stdin_fd, int* stdout_fd);
    /* rpc / shared_buffer: future multi-process server (M8), NULL initially */
};

/* ============================================================
 * Aggregate host descriptor: core (always) + optional desktop extension.
 * ============================================================ */
struct cgui_host {
    cgui_host_core     core;
    cgui_host_desktop* desktop; /* NULL on MCU / simulator without spawn */
    void*               ctx;     /* opaque host context passed to every callback */
};

#ifdef __cplusplus
} /* extern "C" */
#endif
