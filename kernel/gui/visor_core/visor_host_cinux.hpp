/**
 * @file kernel/gui/visor_core/visor_host_cinux.hpp
 * @brief Cinux host adapter -- fills the visor_host table for the in-kernel desktop
 *
 * The Host ABI table (visor_host.h) is the ONLY hard seam between visor core
 * and host. This unit fills that table for the Cinux kernel desktop so visor
 * core can run here: every callback forwards to an existing in-tree facility.
 *
 *   poll_event  -> Mouse::event_queue()          (serialised to visor_event)
 *   now_ms      -> PIT::get_uptime_ms()
 *   alloc/free  -> kmalloc/kfree
 *   log         -> kvprintf
 *   flush       -> (reserved, §4 render engine)
 *   desktop.spawn -> cinux::gui::create_shell_terminal()
 *
 * Swap this fill for an SDL simulator / MCU table fill and the same visor_pump
 * body runs unchanged.
 *
 * Compile condition: CINUX_GUI.
 *
 * Namespace: cinux::gui
 */
#ifndef VISOR_HOST_CINUX_HPP
#define VISOR_HOST_CINUX_HPP

#include "visor_host.h"

#ifdef __cplusplus

namespace cinux::gui {

/**
 * @brief The filled Cinux host descriptor (singleton)
 *
 * Returned by reference so every caller observes the table filled by
 * cinux_visor_host_init(). Reading before init() yields an all-NULL table
 * (safe: visor_pump() NULL-checks every callback it dereferences).
 *
 * @return reference to the static Cinux host descriptor
 */
visor_host& cinux_visor_host();

/**
 * @brief Fill the Cinux host descriptor (call once after gui_init())
 *
 * Wires every core callback + the Desktop extension and binds the opaque ctx.
 * Idempotent: safe to call again to rebind after a subsystem reset.
 */
void cinux_visor_host_init();

}  // namespace cinux::gui

#endif /* __cplusplus */

#endif /* VISOR_HOST_CINUX_HPP */
