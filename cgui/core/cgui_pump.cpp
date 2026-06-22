/**
 * @file cgui/core/cgui_pump.cpp
 * @brief cgui core pump -- one host-neutral GUI iteration
 *
 * Host-neutral: this body has ZERO host includes. It drains raw input events
 * via poll_event, hands each to dispatch_event, asks the host to render one
 * frame (render_frame reports the dirty rects + the staging buffer), then
 * flushes each dirty rect to the display. Everything host-specific (the GUI
 * types, the renderer, the cursor, what counts as "changed") lives in the
 * host adapter that fills the table -- so the same pump body runs on the Cinux
 * kernel today and an SDL simulator / MCU with only the table fill swapped.
 *
 * Compile condition: CINUX_GUI.
 */

#include "cgui_pump.hpp"

#include <stdint.h>

#include "cgui_event.h"

namespace cinux::gui {
namespace {

/* Largest event payload we ever buffer behind a header (pointer = 18 B). */
constexpr uint16_t kMaxPayload    = 24;
/* Cap on dirty rects flushed per frame; the host collapses (bounding box) if
 * it has more -- over-cover is safe, under-cover would drop pixels. */
constexpr uint32_t kMaxDirtyRects = 64;

}  // namespace

void pump(cgui_host* host) {
    if (host == nullptr) {
        return;
    }

    /* 1. Drain all pending input and let the host dispatch each event. The host
     *    deserialises + applies it to its own GUI state; any change surfaces as
     *    dirty rects from render_frame below. */
    if (host->core.poll_event != nullptr && host->core.dispatch_event != nullptr) {
        alignas(uint32_t) uint8_t buf[sizeof(cgui_event_header) + kMaxPayload];
        auto*                     hdr     = reinterpret_cast<cgui_event_header*>(buf);
        const void*               payload = buf + sizeof(cgui_event_header);
        while (host->core.poll_event(host->ctx, hdr, sizeof(buf))) {
            host->core.dispatch_event(host->ctx, hdr, payload);
        }
    }

    /* 2. Render one frame: the host composites into its staging buffer and
     *    reports the dirty rects + staging layout. count==0 = nothing changed
     *    (idle) -> skip the flush entirely. */
    if (host->core.render_frame == nullptr || host->core.flush == nullptr) {
        return;
    }
    cgui_rect  rects[kMaxDirtyRects];
    cgui_frame frame{};
    frame.rects     = rects;
    frame.max_rects = kMaxDirtyRects;
    host->core.render_frame(host->ctx, &frame);

    if (frame.count == 0 || frame.pixels == nullptr) {
        return; /* idle or nothing to present */
    }

    /* 3. Flush each dirty rect from the staging buffer to the display. The
     *    host's flush forwards (framebuffer / SPI / DMA) per its backend. */
    for (uint32_t i = 0; i < frame.count; i++) {
        const cgui_rect& r = frame.rects[i];
        host->core.flush(host->ctx, r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0, frame.pixels,
                         frame.stride, frame.format);
    }
}

}  // namespace cinux::gui
