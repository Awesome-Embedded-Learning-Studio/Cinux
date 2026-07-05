/**
 * @file kernel/gui/desktop_launch.cpp
 * @brief GUI userspace launch (cinux::proc::launch_userspace, CINUX_GUI build)
 *
 * CODING-TASTE §14: this is the GUI-side implementation of the single
 * launch_userspace() interface declared in kernel/proc/userspace.hpp. It starts
 * the desktop (mouse + keyboard listener via gui_start) and spawns the
 * gui_worker thread that drives the core-owned GuiCore session + services the
 * xHCI event ring each frame. The non-GUI counterpart lives in
 * kernel/proc/shell_launch.cpp; CMake links exactly one (the gui/ subdirectory
 * is added only under if(CINUX_GUI)).
 *
 * F13-B (2026-07-05): gui_worker now drives GuiCore::pump() (core owns the
 * staging session) instead of the old free-function cinux::gui::pump().
 * handoff_framebuffer_to_gui calls cinux_host_init() to build the widget tree
 * + GuiCore; the old Canvas + gui_init(canvas, font) steps are gone (the new
 * core owns its staging buffer and carries its own PSF2 font data).
 */

#include <stdint.h>

#include "kernel/drivers/usb/usb_init.hpp"
#include "kernel/drivers/video/console.hpp"
#include "kernel/gui/gui_init.hpp"
#include "kernel/gui/host_cinux.hpp"
#include "kernel/lib/kprintf.hpp"
#include "kernel/proc/scheduler.hpp"
#include "kernel/proc/task_builder.hpp"
#include "kernel/proc/userspace.hpp"
#include "third_party/Cinux-GUI/core/gui_core.hpp"  // GuiCore (complete type for pump())

namespace {

/// GUI render/input pump loop. Drives the core-owned GuiCore session through
/// the Host ABI table, then services the xHCI event ring. Runs for the system
/// lifetime on its own thread.
void gui_worker_thread() {
    cinux::lib::kprintf("[GUI] Worker thread started\n");
    while (true) {
        cinux::gui::cinux_core().pump();
        // Service the xHCI event ring each frame (usb::poll_input is a no-op if
        // no controller was enumerated, or if USB is compiled out -- §14 links
        // usb_stub.cpp). Under nested-KVM the MSI-X transfer-complete interrupt
        // is not reliably delivered, so polling the ring here is the production
        // event-service path; cheap when the mouse is idle.
        cinux::drivers::usb::poll_input();
        cinux::proc::Scheduler::yield();
    }
}

}  // namespace

namespace cinux::proc {

void launch_userspace() {
    cinux::lib::kprintf("[INIT] ===== Milestone 035: GUI Desktop (F13-B) =====\n");

    // Start the GUI: PS/2 mouse + keyboard listener (mirrors key events into
    // the unified Mouse event queue so host_cinux poll_event drains both).
    // Widget tree + GuiCore + mouse bounds are constructed by cinux_host_init
    // (called from handoff_framebuffer_to_gui at kernel_main time).
    cinux::gui::gui_start();

    // Launch the GUI worker thread to drive the render/input pump + deferred
    // work (B2: PTY shell spawn) outside of PIT interrupt context.
    auto* gui_task = TaskBuilder().set_entry(gui_worker_thread).set_name("gui_worker").build();
    if (gui_task != nullptr) {
        Scheduler::add_task(gui_task);
        cinux::lib::kprintf("[INIT] GUI worker thread launched\n");
    }
}

void handoff_framebuffer_to_gui(cinux::drivers::Framebuffer& fb, cinux::drivers::PSFFont& font,
                                cinux::drivers::Console& console) {
    // The new core owns its staging buffer + carries its own PSF2 font data, so
    // the CinuxOS PSFFont is no longer consumed by the GUI path (it stays for
    // the text Console). cinux_host_init builds the HostState widget tree +
    // GuiCore over the framebuffer geometry + fills the host table. Detach the
    // text console so routine logs stop overlaying the desktop (still serial +
    // klog; kpanic re-enables).
    (void)font;
    cinux::gui::cinux_host_init(&fb);
    cinux::lib::kprintf_set_sink_enabled(cinux::drivers::Console::console_sink_adapter, &console,
                                         false);
}

}  // namespace cinux::proc
