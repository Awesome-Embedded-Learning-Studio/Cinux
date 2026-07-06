/**
 * @file kernel/gui/gui_init.cpp
 * @brief GUI subsystem initialisation implementation (F13-B)
 *
 * F13-B (2026-07-05): the old WindowManager singleton init + desktop-icon
 * registration + create_shell_terminal() (PTY + TaskBuilder + launch_user_program)
 * are gone -- the widget tree now lives in the host adapter (HostState, built by
 * cinux_host_init), and shell spawn is deferred to B2's HostDesktop::spawn.
 * What remains here is the input-side wiring: PS/2 mouse + a keyboard listener
 * that mirrors each KeyEvent into the unified Mouse event queue so host_cinux
 * poll_event drains both pointer + keyboard through one queue.
 */

#include "gui_init.hpp"

#include "kernel/drivers/input/input_event_device.hpp"  // /dev/event0 push (F-GUI b2)
#include "kernel/drivers/keyboard/keyboard.hpp"
#include "kernel/drivers/mouse/mouse.hpp"
#include "kernel/gui/event.hpp"  // Event / EventType (queue element)
#include "kernel/lib/kprintf.hpp"

namespace cinux::gui {

namespace {

// Key listener registered with the keyboard driver: mirror each decoded
// KeyEvent into the GUI EventQueue so host_cinux poll_event sees keyboard input
// through the unified Mouse queue. Lives here (not in keyboard.cpp) so the
// keyboard driver has no GUI/EventQueue dependency -- it just calls a
// registered listener (CODING-TASTE §14).
void on_key_event(const cinux::drivers::KeyEvent& ev) {
    cinux::gui::Event gui_ev{};
    gui_ev.type_     = ev.pressed ? cinux::gui::EventType::KeyDown : cinux::gui::EventType::KeyUp;
    gui_ev.key.ascii = ev.ascii;
    gui_ev.key.scancode = ev.scancode;
    gui_ev.key.pressed  = ev.pressed;
    gui_ev.key.shift    = ev.shift;
    gui_ev.key.ctrl     = ev.ctrl;
    gui_ev.key.alt      = ev.alt;
    cinux::drivers::Mouse::event_queue().enqueue(gui_ev);
    // F-GUI-USERSPACE batch 2: also deliver keyboard events to /dev/event0 so a
    // userspace GUI host can read them.  Zero touch to keyboard.cpp (the driver
    // stays GUI-free; this listener is the seam).
    cinux::input::InputEventDevice::instance().push_event(gui_ev);
}

}  // namespace

void gui_start() {
    cinux::lib::kprintf("[GUI] ===== Milestone 033: GUI Desktop (F13-B new core) =====\n");

    // Initialise PS/2 mouse driver. Screen bounds are set later by
    // cinux_host_init (it owns display geometry now).
    cinux::drivers::Mouse::init();
    // Dual-dispatch keys into the unified queue via a listener (the keyboard
    // has no #ifdef CINUX_GUI; it just calls whoever registered -- §14).
    cinux::drivers::Keyboard::register_key_listener(on_key_event);

    cinux::lib::kprintf(
        "[GUI] Mouse + keyboard listener initialised; desktop driven by "
        "gui_worker pump loop.\n");
}

}  // namespace cinux::gui
