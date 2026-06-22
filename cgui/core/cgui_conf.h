/**
 * @file cgui/core/cgui_conf.h
 * @brief cgui compile-time profile / config macros (lv_conf.h-style gating)
 *
 * DRAFT v2. All trimming is compile-time #if -- runtime config is a luxury on
 * MCU. One cgui_conf.h (or -D flags) pins the binary shape. See presets §2
 * for the full macro list. This header only sets profile defaults; per-feature
 * CGUI_USE_* macros are added as the toolkit grows (M5+).
 */
#pragma once

/* ---- Buffer / present modes ---- */
#define CGUI_BUFFER_FULL        0 /* whole-frame double buffer (Desktop) */
#define CGUI_BUFFER_PARTIAL     1 /* 1/N screen, flush dirty blocks (MCU-Color) */
#define CGUI_BUFFER_STREAM_PAGE 2 /* 8-row page band, picture loop (MCU-F1 OLED) */

/* ============================================================
 * Profile selection (exactly one). Default to Desktop if none set.
 * ============================================================ */
#if !defined(CGUI_PROFILE_DESKTOP) && !defined(CGUI_PROFILE_MCU_F1) &&                           \
    !defined(CGUI_PROFILE_MCU_COLOR)
#    define CGUI_PROFILE_DESKTOP 1
#endif

/* ============================================================
 * Per-profile defaults (overridable by -D before including this header).
 * ============================================================ */
#if defined(CGUI_PROFILE_DESKTOP)
#    ifndef CGUI_COLOR_DEPTH
#        define CGUI_COLOR_DEPTH 32
#    endif
#    ifndef CGUI_BUFFER_MODE
#        define CGUI_BUFFER_MODE CGUI_BUFFER_FULL
#    endif
#    ifndef CGUI_USE_TERMINAL
#        define CGUI_USE_TERMINAL 1
#    endif
#    define CGUI_PROFILE_NAME "Desktop"

#elif defined(CGUI_PROFILE_MCU_F1)
#    ifndef CGUI_COLOR_DEPTH
#        define CGUI_COLOR_DEPTH 1
#    endif
#    ifndef CGUI_BUFFER_MODE
#        define CGUI_BUFFER_MODE CGUI_BUFFER_STREAM_PAGE
#    endif
#    define CGUI_NO_FPU       1
#    define CGUI_USE_TERMINAL 0
#    define CGUI_PROFILE_NAME "MCU-F1"

#elif defined(CGUI_PROFILE_MCU_COLOR)
#    ifndef CGUI_COLOR_DEPTH
#        define CGUI_COLOR_DEPTH 16
#    endif
#    ifndef CGUI_BUFFER_MODE
#        define CGUI_BUFFER_MODE CGUI_BUFFER_PARTIAL
#    endif
#    define CGUI_USE_TERMINAL 0
#    define CGUI_PROFILE_NAME "MCU-Color"
#endif
