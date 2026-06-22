/**
 * @file kernel/gui/visor_core/visor_event.h
 * @brief visor ABI -- fixed-width event header + variable tail (cross-privilege stable)
 *
 * DRAFT v2 ABI. Replaces the bare union Event in kernel/gui/event.hpp with a
 * versioned, packed header so a cross-privilege (kernel / user-space / MCU)
 * ABI mismatch cannot overrun: the header is the contract, the tail is
 * interpreted by (type, version, payload_len).
 *
 * Freestanding C header (no kernel internals, no C++).
 *
 * Compile condition: part of visor core (CINUX_GUI tree).
 */
#ifndef VISOR_EVENT_H
#define VISOR_EVENT_H

#include <stdint.h>

#define VISOR_EVENT_MAGIC 0x5253u /* 'RS' -- endian/version sanity check */
#define VISOR_ABI_VERSION 1u

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VISOR_EVENT_POINTER = 1, /* mouse / touch: abs + delta + buttons */
    VISOR_EVENT_KEYCODE = 2, /* keyboard: scancode + ascii + modifiers */
    VISOR_EVENT_ENCODER = 3, /* rotary encoder: axis diff */
    VISOR_EVENT_TOUCH   = 4, /* multi-slot touch */
} visor_event_type;

/* flags bits in visor_event_header.flags */
#define VISOR_EVENT_FLAG_PRESSED       (1u << 0) /* key/button down vs up */
#define VISOR_EVENT_FLAG_CONTINUE_READ (1u << 1) /* more data buffered; call poll again */

/**
 * Fixed-width event header. Packed, no padding. The variable-length payload
 * follows in memory; poll_event reads the header first, then interprets the
 * tail by (type, version, payload_len) -- a version mismatch never reads past
 * payload_len bytes.
 */
typedef struct __attribute__((packed)) {
    uint16_t magic;       /* VISOR_EVENT_MAGIC */
    uint16_t version;     /* VISOR_ABI_VERSION */
    uint8_t  type;        /* visor_event_type */
    uint8_t  flags;       /* VISOR_EVENT_FLAG_* bitmask */
    uint16_t payload_len; /* tail byte count */
    /* variable-length payload follows */
} visor_event_header;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VISOR_EVENT_H */
