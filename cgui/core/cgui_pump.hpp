/**
 * @file cgui/core/cgui_pump.hpp
 * @brief cgui core pump -- one host-neutral GUI iteration through the Host ABI
 *
 * pump() is the core-side partner of the host adapter: it has ZERO host
 * includes. It drains raw input events (poll_event), hands each to the host
 * (dispatch_event), asks the host to render one frame (render_frame reports the
 * dirty rects + staging buffer), then flushes each dirty rect to the display
 * (flush). Everything host-specific -- the GUI types, renderer, cursor, what
 * counts as "changed" -- lives in the host adapter that fills the table. That
 * is the "not aware of user vs kernel mode" mechanism: the same pump body
 * drives the Cinux kernel today and an SDL simulator / MCU with only the table
 * fill swapped.
 *
 * Compile condition: CINUX_GUI.
 *
 * Namespace: cinux::gui
 */
#pragma once

#include "cgui_host.h"

#ifdef __cplusplus

namespace cinux::gui {

/**
 * @brief Run one cgui pump iteration through the Host ABI table
 *
 * Sequence:
 *   1. Drain all input via host->core.poll_event(); hand each event to
 *      host->core.dispatch_event() (the host deserialises + applies it).
 *   2. host->core.render_frame(): the host composites into its staging buffer
 *      and reports the dirty rects + staging layout in a cgui_frame.
 *   3. Flush each dirty rect via host->core.flush() (count==0 = idle, skip).
 *
 * Defensive discipline: a NULL host returns immediately, and every host
 * callback the body dereferences (poll_event / dispatch_event / render_frame /
 * flush) is NULL-checked first. The pump is therefore safe against a
 * partially-filled table.
 *
 * @param host  filled host descriptor (NULL -> no-op)
 */
void pump(cgui_host* host);

}  // namespace cinux::gui

#endif /* __cplusplus */
