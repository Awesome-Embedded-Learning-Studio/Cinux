/**
 * @file kernel/gui/visor_core/visor_pump.hpp
 * @brief visor core pump -- one GUI iteration driven through the Host ABI table
 *
 * visor_pump() is the core-side partner of the host adapter: it never touches
 * the framebuffer / IRQ / mouse queue / process structures directly -- every
 * input read, time read and spawn goes through the visor_host table. That is
 * the "not aware of user vs kernel mode" mechanism: the same pump body drives
 * the Cinux desktop today and a future user-space server / MCU with only the
 * table fill swapped.
 *
 * §3b scope (see document/todo/f13-gui/visor-02-refactor-and-separation.md §3):
 * this is the shape skeleton. Input is read + round-tripped through the visor
 * event ABI, but rendering still calls the legacy WindowManager::composite()
 * path -- the visor render engine (SwRaster + staging buffer + dirty region)
 * takes over in §4. Behaviour is therefore unchanged vs gui_pump().
 *
 * Compile condition: CINUX_GUI.
 *
 * Namespace: cinux::gui
 */
#ifndef VISOR_PUMP_HPP
#define VISOR_PUMP_HPP

#include "visor_host.h"

#ifdef __cplusplus

namespace cinux::gui {

/**
 * @brief Run one visor pump iteration through the Host ABI table
 *
 * Sequence:
 *   1. Drain all input via host->core.poll_event(), deserialise each visor
 *      event back into a cinux::gui::Event, dispatch to the window manager.
 *   2. Act on a deferred desktop-icon click via host->desktop->spawn() (the
 *      Desktop extension), falling back to the in-tree helper if absent.
 *   3. Poll terminal output and composite the frame (legacy path until §4).
 *
 * Defensive discipline: a NULL host returns immediately, and every host
 * callback the body dereferences (poll_event / now_ms / desktop / desktop->spawn)
 * is NULL-checked first. The pump is therefore safe against a partially-filled
 * table (e.g. an MCU profile with no Desktop extension, or a host that omits
 * now_ms). Host time is read here -- not by the caller -- so every host-table
 * dereference lives inside the pump behind a uniform NULL guard.
 *
 * @param host  filled host descriptor (NULL -> no-op; desktop may be NULL on
 *              MCU, in which case the icon action uses the fallback)
 */
void visor_pump(visor_host* host);

}  // namespace cinux::gui

#endif /* __cplusplus */

#endif /* VISOR_PUMP_HPP */
