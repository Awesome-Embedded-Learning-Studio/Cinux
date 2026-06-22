/**
 * @file kernel/gui/visor_core/visor_host.h
 * @brief visor Host ABI -- the ONLY hard seam between visor core and host
 *
 * DRAFT v2 ABI. visor core never touches framebuffer / IRQ / syscall / process
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
#ifndef VISOR_HOST_H
#define VISOR_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "visor_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Pixel format (v2 hard contract: stride / endianness / premultiplied alpha
 * / byte-bit order -- aligned to Wayland shm rigour; see presets §4).
 * ============================================================ */
typedef enum {
    VISOR_PIX_XRGB8888 = 1, /* Desktop, 32bpp (no alpha) */
    VISOR_PIX_ARGB8888 = 2, /* 32bpp, premultiplied alpha */
    VISOR_PIX_RGB565   = 3, /* MCU-Color, 16bpp */
    VISOR_PIX_1BPP     = 4, /* MCU-F1 mono OLED, alpha-mask (NOT colorkey) */
} visor_pixel_format;

/* ============================================================
 * Core host table -- every host fills this (MCU has no Desktop part).
 * ============================================================ */
typedef struct {
    /* ---- L1 Display backend: flush model (v2) ----
     * Core owns the staging/render buffer. After rendering a rect it pushes
     * the pixels to the backend for display. Replaces begin_frame->pointer
     * (unsafe for STREAM-PAGE / SPI DMA / user-space shared buffer). */
    void (*flush)(void* ctx, int x, int y, int w, int h, const void* pixels, uint32_t stride,
                  visor_pixel_format fmt);
    void (*flush_complete)(void* ctx); /* host -> core: last async flush done, buffer reusable */
    /* power state (MCU sleep / display off) */
    void (*enter_sleep)(void* ctx);
    void (*exit_sleep)(void* ctx);

    /* ---- L2 Input backend ---- */
    bool (*poll_event)(void* ctx, visor_event_header* out, uint16_t out_cap);

    /* ---- L2 Time backend ---- */
    uint32_t (*now_ms)(void* ctx);
    uint32_t (*next_deadline_ms)(void* ctx); /* for MCU __WFI throttling */

    /* ---- Memory / Log (all hosts have these) ---- */
    void* (*alloc)(void* ctx, size_t n);
    void (*free)(void* ctx, void* p);
    void (*log)(void* ctx, const char* fmt, ...);
} visor_host_core;

/* ============================================================
 * Desktop extension -- Desktop profile only; NULL on MCU.
 * ============================================================ */
typedef struct {
    /* spawn a child process, returning its stdio handles (Desktop only). */
    int (*spawn)(void* ctx, const char* path, char* const argv[], int* stdin_fd, int* stdout_fd);
    /* rpc / shared_buffer: future multi-process server (M8), NULL initially */
} visor_host_desktop;

/* ============================================================
 * Aggregate host descriptor: core (always) + optional desktop extension.
 * ============================================================ */
typedef struct {
    visor_host_core     core;
    visor_host_desktop* desktop; /* NULL on MCU / simulator without spawn */
    void*               ctx;     /* opaque host context passed to every callback */
} visor_host;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VISOR_HOST_H */
