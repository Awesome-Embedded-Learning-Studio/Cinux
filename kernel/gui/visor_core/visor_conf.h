/**
 * @file kernel/gui/visor_core/visor_conf.h
 * @brief visor compile-time profile / config macros (lv_conf.h-style gating)
 *
 * DRAFT v2. All trimming is compile-time #if -- runtime config is a luxury on
 * MCU. One visor_conf.h (or -D flags) pins the binary shape. See presets §2
 * for the full macro list. This header only sets profile defaults; per-feature
 * VISOR_USE_* macros are added as the toolkit grows (M5+).
 */
#ifndef VISOR_CONF_H
#define VISOR_CONF_H

/* ---- Buffer / present modes ---- */
#define VISOR_BUFFER_FULL        0  /* whole-frame double buffer (Desktop) */
#define VISOR_BUFFER_PARTIAL     1  /* 1/N screen, flush dirty blocks (MCU-Color) */
#define VISOR_BUFFER_STREAM_PAGE 2  /* 8-row page band, picture loop (MCU-F1 OLED) */

/* ============================================================
 * Profile selection (exactly one). Default to Desktop if none set.
 * ============================================================ */
#if !defined(VISOR_PROFILE_DESKTOP) && !defined(VISOR_PROFILE_MCU_F1) && \
    !defined(VISOR_PROFILE_MCU_COLOR)
#    define VISOR_PROFILE_DESKTOP 1
#endif

/* ============================================================
 * Per-profile defaults (overridable by -D before including this header).
 * ============================================================ */
#if defined(VISOR_PROFILE_DESKTOP)
#  ifndef VISOR_COLOR_DEPTH
#    define VISOR_COLOR_DEPTH 32
#  endif
#  ifndef VISOR_BUFFER_MODE
#    define VISOR_BUFFER_MODE VISOR_BUFFER_FULL
#  endif
#  ifndef VISOR_USE_TERMINAL
#    define VISOR_USE_TERMINAL 1
#  endif
#  define VISOR_PROFILE_NAME "Desktop"

#elif defined(VISOR_PROFILE_MCU_F1)
#  ifndef VISOR_COLOR_DEPTH
#    define VISOR_COLOR_DEPTH 1
#  endif
#  ifndef VISOR_BUFFER_MODE
#    define VISOR_BUFFER_MODE VISOR_BUFFER_STREAM_PAGE
#  endif
#  define VISOR_NO_FPU 1
#  define VISOR_USE_TERMINAL 0
#  define VISOR_PROFILE_NAME "MCU-F1"

#elif defined(VISOR_PROFILE_MCU_COLOR)
#  ifndef VISOR_COLOR_DEPTH
#    define VISOR_COLOR_DEPTH 16
#  endif
#  ifndef VISOR_BUFFER_MODE
#    define VISOR_BUFFER_MODE VISOR_BUFFER_PARTIAL
#  endif
#  define VISOR_USE_TERMINAL 0
#  define VISOR_PROFILE_NAME "MCU-Color"
#endif

#endif  /* VISOR_CONF_H */
