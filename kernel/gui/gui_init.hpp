/**
 * @file kernel/gui/gui_init.hpp
 * @brief GUI subsystem initialisation interface (F13-B)
 *
 * F13-B (2026-07-05): the old gui_init(Canvas&, PSFFont&) + create_shell_terminal()
 * helpers are gone. The widget tree + GuiCore are constructed by the host
 * adapter (cinux_host_init, called from handoff_framebuffer_to_gui); shell
 * spawn arrives in B2 via the HostDesktop::spawn callback. gui_start() remains
 * to register the PS/2 mouse + keyboard listener.
 *
 * This header is only compiled when CINUX_GUI is defined.
 *
 * Namespace: cinux::gui
 */

#pragma once

namespace cinux::gui {

/**
 * @brief Register the PS/2 mouse driver + keyboard listener (call from
 *        kernel_init_thread via launch_userspace)
 *
 * After this, every decoded key event is mirrored into the unified Mouse event
 * queue so host_cinux poll_event drains both pointer + keyboard. Widget tree
 * + GuiCore + mouse screen bounds are set by cinux_host_init() (called earlier
 * from handoff_framebuffer_to_gui at kernel_main time).
 */
void gui_start();

}  // namespace cinux::gui
