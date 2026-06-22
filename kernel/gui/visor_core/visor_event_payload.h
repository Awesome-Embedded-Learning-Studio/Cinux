/**
 * @file kernel/gui/visor_core/visor_event_payload.h
 * @brief visor ABI -- typed payloads that follow visor_event_header
 *
 * DRAFT v2 ABI. visor_event.h defines the fixed-width 8-byte header; this
 * header defines the payload layouts that follow it, keyed by header.type.
 * Together (header + payload) they are the cross-privilege event contract:
 * the header is interpreted unconditionally, the tail only by (type, version,
 * payload_len) -- a version/length mismatch never reads past payload_len.
 *
 * Why payloads carry a discriminator the header cannot encode:
 *   - POINTER: the header PRESSED flag alone cannot distinguish move vs
 *     button-down vs button-up (a host's window manager needs all three).
 *     visor_pointer_payload.kind carries that, so the header stays minimal.
 *   - KEYCODE: press/release is the header PRESSED flag (orthogonal to the
 *     key identity), so the payload only carries identity + modifiers.
 *
 * Freestanding C header (no kernel internals, no C++).
 *
 * Compile condition: part of visor core (CINUX_GUI tree).
 */
#ifndef VISOR_EVENT_PAYLOAD_H
#define VISOR_EVENT_PAYLOAD_H

#include <stdint.h>

#include "visor_event.h" /* visor_event_type values */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Pointer-event kind (visor_pointer_payload.kind).
 * The header PRESSED flag is not enough to tell move/down/up apart.
 * ============================================================ */
#define VISOR_POINTER_KIND_MOVE 0 /* cursor moved, no button change */
#define VISOR_POINTER_KIND_DOWN 1 /* a button transitioned to pressed */
#define VISOR_POINTER_KIND_UP   2 /* a button transitioned to released */

/* ============================================================
 * Key-code modifier bits (visor_keycode_payload.modifiers).
 * Press/release is carried by the header PRESSED flag, not here.
 * ============================================================ */
#define VISOR_KEYMOD_SHIFT (1u << 0)
#define VISOR_KEYMOD_CTRL  (1u << 1)
#define VISOR_KEYMOD_ALT   (1u << 2)

/* ============================================================
 * POINTER payload: follows the header when type == VISOR_EVENT_POINTER.
 * Packed, no padding (cross-privilege layout).
 * ============================================================ */
typedef struct __attribute__((packed)) {
    uint8_t kind;        /* VISOR_POINTER_KIND_* */
    int32_t x;           /* absolute cursor X (pixels) */
    int32_t y;           /* absolute cursor Y (pixels) */
    int32_t dx;          /* relative X since last pointer event */
    int32_t dy;          /* relative Y since last pointer event (positive = down) */
    uint8_t buttons;     /* bitmask: bit0=left, bit1=right, bit2=middle */
} visor_pointer_payload; /* 1 + 4*4 + 1 = 18 bytes */

/* ============================================================
 * KEYCODE payload: follows the header when type == VISOR_EVENT_KEYCODE.
 * Packed, no padding (cross-privilege layout).
 * ============================================================ */
typedef struct __attribute__((packed)) {
    char    ascii;       /* 0 if non-printable */
    uint8_t scancode;    /* raw scan code set 1 */
    uint8_t modifiers;   /* VISOR_KEYMOD_* bitmask */
} visor_keycode_payload; /* 3 bytes */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VISOR_EVENT_PAYLOAD_H */
