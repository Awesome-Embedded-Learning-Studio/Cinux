/**
 * @file    uart_input.cpp
 * @brief   UART bytes in, key events out: event0's honest source.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include "kernel/arch/armv7/platform/serial.hpp"
#include "kernel/drivers/tty/console_tty.hpp"
#include "kernel/drivers/usb/usb_keyboard.hpp"
#include "kernel/arch/armv7/uart_input.hpp"

#include <cstdint>

#include "kernel/drivers/input/event_stream.hpp"
#include "kernel/drivers/input/input_config.hpp"

namespace cinux::armv7 {

namespace {

driver::EventStream g_keyboard_stream;
driver::EventStream g_pointer_stream;


}  // namespace

namespace {

/// The named keys the escape decoder and the ctrl folding need.
constexpr uint16_t kKeyLeftCtrl = 29;
constexpr uint16_t kKeyUp       = 103;
constexpr uint16_t kKeyDown     = 108;
constexpr uint16_t kKeyLeft     = 105;
constexpr uint16_t kKeyRight    = 106;

/// One key and whether a shift has to ride it: the shifted symbols are
/// their key's twin, so a shell line's quote or pipe is a pair of words.
struct KeyStroke {
    uint16_t code = 0;
    bool     shift = false;
};

/// The serial line's ASCII bytes, in the evdev codes a keymap expects.
/// Unshifted letters and digits plus the editing keys are enough for a
/// shell; anything else is swallowed rather than guessed.
uint16_t evdev_code_for(char character) {
    // Linux's evdev letter codes are not sequential: the top row, the
    // home row and the bottom row each walk their own run, so a table is
    // the honest form. A sequential guess turns "echo" into "gdkz".
    static constexpr uint16_t kLetterCodes[26] = {
        30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,  // a..m
        49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  // n..z
    };
    if (character >= 'a' && character <= 'z') {
        return kLetterCodes[static_cast<unsigned long>(character - 'a')];
    }
    if (character >= 'A' && character <= 'Z') {
        return kLetterCodes[static_cast<unsigned long>(character - 'A')];
    }
    if (character >= '1' && character <= '9') {
        return static_cast<uint16_t>(2 + (character - '1'));
    }
    if (character == '0') {
        return 11;
    }
    return 0;
}

/// The punctuation, in the same walk: a shifted symbol names its key.
KeyStroke punctuation_for(char character) {
    switch (character) {
        case ' ': return {57, false};
        case '-': return {12, false};
        case '=': return {13, false};
        case '[': return {26, false};
        case ']': return {27, false};
        case '\\': return {43, false};
        case ';': return {39, false};
        case '\'': return {40, false};
        case ',': return {51, false};
        case '.': return {52, false};
        case '/': return {53, false};
        case '`': return {41, false};
        case '\t': return {15, false};
        case '\r':
        case '\n': return {28, false};
        case '\b': return {14, false};
        case 0x7F: return {14, false};
        case '!': return {2, true};
        case '"': return {40, true};
        case '#': return {4, true};
        case '$': return {5, true};
        case '%': return {6, true};
        case '&': return {8, true};
        case '(': return {10, true};
        case ')': return {11, true};
        case '*': return {9, true};
        case '+': return {13, true};
        case ':': return {39, true};
        case '<': return {51, true};
        case '>': return {52, true};
        case '?': return {53, true};
        case '@': return {3, true};
        case '^': return {7, true};
        case '_': return {12, true};
        case '{': return {26, true};
        case '|': return {43, true};
        case '}': return {27, true};
        case '~': return {41, true};
        default: return {};
    }
}

}  // namespace

namespace {

constexpr uint16_t kKeyLeftShift = 42;

/// One key press and its release, the pair every consumer expects. A
/// shifted symbol carries the modifier: down, the key, the key up, then
/// the modifier up.
void emit_key(uint16_t code, bool shift = false) {
    if (shift) {
        g_keyboard_stream.publish(driver::kEvKey, kKeyLeftShift, 1);
    }
    g_keyboard_stream.publish(driver::kEvKey, code, 1);
    g_keyboard_stream.publish(driver::kEvKey, code, 0);
    if (shift) {
        g_keyboard_stream.publish(driver::kEvKey, kKeyLeftShift, 0);
    }
    g_keyboard_stream.publish_report();
}

/// Where a terminal's escape words put the decoder: a byte arrives, and
/// the next one decides what it means.
enum class EscapeState : unsigned char { kGround, kEscape, kCsi };

EscapeState g_escape = EscapeState::kGround;

/// A control byte is Ctrl plus a letter: the modifier goes down first,
/// the letter rides it, and the modifier lifts last.
void emit_ctrl_key(uint16_t code) {
    g_keyboard_stream.publish(driver::kEvKey, kKeyLeftCtrl, 1);
    g_keyboard_stream.publish(driver::kEvKey, code, 1);
    g_keyboard_stream.publish(driver::kEvKey, code, 0);
    g_keyboard_stream.publish(driver::kEvKey, kKeyLeftCtrl, 0);
    g_keyboard_stream.publish_report();
}

/// The four arrows, in the words a terminal sends and the codes the
/// desk's screen forwards as CSI words.
uint16_t arrow_code_for(char final) {
    switch (final) {
        case 'A': return kKeyUp;
        case 'B': return kKeyDown;
        case 'C': return kKeyRight;
        case 'D': return kKeyLeft;
        default: return 0;
    }
}

}  // namespace

void NoteSerialByte(char character) {
    // Escape words first: a terminal that reads a bare ESC as a key would
    // turn every arrow into three stray letters.
    switch (g_escape) {
        case EscapeState::kEscape:
            if (character == '[') {
                g_escape = EscapeState::kCsi;
            } else {
                g_escape = EscapeState::kGround;
            }
            return;
        case EscapeState::kCsi:
            g_escape = EscapeState::kGround;
            if (const uint16_t kArrow = arrow_code_for(character); kArrow != 0) {
                emit_key(kArrow);
            }
            return;
        case EscapeState::kGround: break;
    }
    if (character == 0x1B) {
        g_escape = EscapeState::kEscape;
        return;
    }
    // A control byte below space is Ctrl plus a letter, except the three
    // the line discipline reads as itself.
    const auto kByte = static_cast<unsigned char>(character);
    if (kByte > 0 && kByte < 0x20 && kByte != '\t' && kByte != '\n' && kByte != '\r' &&
        kByte != '\b') {
        const char          kLetter = static_cast<char>('a' + (kByte - 1));
        const uint16_t      kCode   = evdev_code_for(kLetter);
        if (kCode != 0) {
            emit_ctrl_key(kCode);
        }
        return;
    }
    if (const uint16_t kCode = evdev_code_for(character); kCode != 0) {
        emit_key(kCode);
        return;
    }
    const KeyStroke kStroke = punctuation_for(character);
    if (kStroke.code != 0) {
        emit_key(kStroke.code, kStroke.shift);
    }
}

void PollSerialKeyboard() {
    // The receive FIFO is thirty-two bytes deep: a line pasted at once
    // has to come out in one pass, or the tail overflows and the shell
    // never sees its newline.
    for (unsigned long budget = 0; budget < 32; ++budget) {
        char character = 0;
        if (!cinux::platform::SerialTryGetChar(&character) &&
            !cinux::driver::UsbKeyboard::self().take(&character)) {
            return;
        }
        // The console queue still gets the byte: whichever consumer
        // asked first cannot starve the other, because both are fed
        // here, at the source.
        // The byte source already published it; the pump only keeps the
        // console's queue in step, or one key would reach the desk twice.
        static_cast<void>(cinux::driver::ConsoleTty().feed(character));
    }
}

driver::EventStream& UartKeyboardEvents() {
    return g_keyboard_stream;
}

driver::EventStream& BoardPointerEvents() {
    return g_pointer_stream;
}

}  // namespace cinux::armv7
