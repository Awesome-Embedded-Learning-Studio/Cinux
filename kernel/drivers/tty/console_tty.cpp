#include "kernel/drivers/tty/console_tty.hpp"

#include "kernel/console/console.hpp"
#include "kernel/console/screen.hpp"
#include "kernel/drivers/serial/serial.hpp"
#include "kernel/drivers/tty/tty.hpp"
#include "kernel/drivers/tty/tty_config.hpp"
#include "kernel/proc/scheduler.hpp"

namespace cinux::driver {

namespace {

/// The port's keyboard tap, installed before the first console read.
ConsoleKeyTap g_key_tap{};

/// The serial line first — the hosted world types there — then
/// whatever keyboard the port sat behind the tap.
bool console_take_key(void* /*context*/, char* out) {
    if (SerialTryGetChar(out)) {
        return true;
    }
    if (g_key_tap.take != nullptr && g_key_tap.take(out)) {
        return true;
    }
    return false;
}

bool console_has_key(void* /*context*/) {
    return SerialHasKey() || (g_key_tap.has != nullptr && g_key_tap.has());
}

void console_echo(void* /*context*/, char key) {
    console::PutChar(key);
}

void console_wait(void* /*context*/) {
    proc::Scheduler::self().yield();
}

const TtyWiring kConsoleWiring{
    .context  = nullptr,
    .take_key = console_take_key,
    .has_key  = console_has_key,
    .echo     = console_echo,
    .wait     = console_wait,
};

/// Shapes the terminal's window from the screen, once it has one.
void shape_window(Tty& terminal) {
    const auto kRows    = static_cast<unsigned short>(console::TextConsole::self().rows());
    const auto kColumns = static_cast<unsigned short>(console::TextConsole::self().columns());
    terminal.set_window(TtyWindow{
        .rows     = kRows,
        .columns  = kColumns,
        .pixels_x = 0,
        .pixels_y = 0,
    });
}

}  // namespace

void SetConsoleKeyTap(const ConsoleKeyTap& tap) {
    g_key_tap = tap;
}

Tty& ConsoleTty() {
    static constinit Tty terminal{};
    if (!terminal.wired()) {
        terminal.wire(kConsoleWiring);
    }
    if (terminal.window().rows == 0) {
        shape_window(terminal);
    }
    return terminal;
}

}  // namespace cinux::driver
