/**
 * @file cgui/core/cgui_abi_check.cpp
 * @brief Compile-time ABI self-check for the cgui Host ABI
 *
 * Includes the ABI headers and static_asserts the cross-privilege layout
 * contracts (header size, packed-ness, aggregate carries extension + ctx).
 * This is the machine-verified guarantee that the ABI is stable across
 * kernel / user-space / MCU builds.
 *
 * No runtime effect -- pure compile-time checks. Updated when the pump was
 * decoupled to a host-neutral core: the core now drives the host via
 * dispatch_event + render_frame + flush and never includes a host header.
 *
 * Compile condition: CINUX_GUI.
 */
#include "cgui_conf.h"
#include "cgui_event.h"
#include "cgui_event_payload.h"
#include "cgui_host.h"

#ifdef CINUX_GUI

/* The fixed-width event header MUST be 8 bytes (u16+u16+u8+u8+u16, packed).
 * A cross-privilege ABI drift here would be caught at compile time. */
static_assert(sizeof(cgui_event_header) == 8,
              "cgui_event_header must be exactly 8 bytes (packed, cross-privilege ABI)");

/* cgui_rect is 4x int32 with no padding -- the dirty-rect wire shape. */
static_assert(sizeof(cgui_rect) == 16, "cgui_rect must be 16 bytes (4x int32)");

static_assert(sizeof(cgui_host_core) > 0, "cgui_host_core must be non-empty");
static_assert(sizeof(cgui_host_desktop) > 0, "cgui_host_desktop must be non-empty");

/* The aggregate host descriptor carries core + desktop pointer + ctx, so it
 * must be strictly larger than just the core table. */
static_assert(sizeof(cgui_host) > sizeof(cgui_host_core),
              "cgui_host must carry core + desktop pointer + opaque ctx");

/* Typed payloads that follow the header (§3b). Cross-privilege layout
 * contract: a drift in size or packed-ness is caught at compile time. */
static_assert(sizeof(cgui_pointer_payload) == 18,
              "cgui_pointer_payload must be 18 bytes (kind + 4*int32 + buttons, packed)");
static_assert(sizeof(cgui_keycode_payload) == 3,
              "cgui_keycode_payload must be 3 bytes (ascii + scancode + modifiers, packed)");

/* A profile name must have been selected by cgui_conf.h. */
#    ifdef CGUI_PROFILE_NAME
static_assert(CGUI_COLOR_DEPTH == 1 || CGUI_COLOR_DEPTH == 8 || CGUI_COLOR_DEPTH == 16 ||
                  CGUI_COLOR_DEPTH == 32,
              "CGUI_COLOR_DEPTH must be one of {1,8,16,32}");
#    endif

#endif /* CINUX_GUI */
