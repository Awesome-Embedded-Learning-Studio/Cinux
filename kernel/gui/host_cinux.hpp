/**
 * @file kernel/gui/host_cinux.hpp
 * @brief Cinux host adapter -- fills the Host table for the in-kernel desktop
 *
 * The Host ABI table (third_party/Cinux-GUI/core/host.hpp) is the ONLY hard
 * seam between cinux::gui core and host. This unit fills that table for the
 * Cinux kernel desktop so the new core (P0-P7: Desktop + widget tree + own
 * staging buffer) runs here. Every callback forwards to an existing in-tree
 * facility:
 *
 *   poll_event    -> Mouse::event_queue()         (serialised to EventHeader)
 *   now_ms        -> PIT::get_uptime_ms()
 *   alloc/free    -> kmalloc/kfree
 *   log           -> kvprintf
 *   flush         -> forward dirty rect: core staging buffer -> VBE framebuffer
 *   render_frame  -> Desktop::render(widget tree -> core staging)
 *   dispatch_event-> Desktop::dispatch_pointer/dispatch_key
 *
 * F13-B (2026-07-05): switched from the old visor-pump wm.composite() path to
 * the new core Desktop::render. desktop=NULL here (spawn arrives in B2 with a
 * real kernel PTY + TerminalWidget).
 *
 * Compile condition: CINUX_GUI.
 * Namespace: cinux::gui
 */
#pragma once

#include "third_party/Cinux-GUI/core/host.hpp"

#ifdef __cplusplus

namespace cinux::drivers {
class Framebuffer;
}

namespace cinux::gui {

class GuiCore;  // forward (core/gui_core.hpp) -- core-owned staging session

/**
 * @brief The filled Cinux host descriptor (singleton)
 *
 * Returned by reference so every caller observes the table filled by
 * cinux_host_init(). Reading before init() yields an all-NULL table
 * (safe: pump() NULL-checks every callback it dereferences).
 */
Host& cinux_host();

/**
 * @brief The core-owned GUI session constructed by cinux_host_init()
 *
 * GuiCore owns the staging Surface; pump() drains input through the host
 * table, asks render_frame to paint the widget tree into staging, collects the
 * dirty Region, and flushes each rect. The gui_worker thread calls
 * cinux_core().pump() each iteration.
 */
GuiCore& cinux_core();

/**
 * @brief Fill the host table + construct the widget tree + GuiCore (call once)
 *
 * Wires every core callback (desktop=NULL in B1 -- spawn arrives in B2 with a
 * real PTY), constructs the static HostState (PsfFont + Theme + WindowManager
 * + Window + Label + Desktop) and the GuiCore over the display geometry, and
 * sets mouse screen bounds. Idempotent.
 *
 * @param fb  The VBE framebuffer to forward flushed rects to (may be null,
 *            in which case flush is a no-op)
 */
void cinux_host_init(cinux::drivers::Framebuffer* fb = nullptr);

}  // namespace cinux::gui

#endif /* __cplusplus */
