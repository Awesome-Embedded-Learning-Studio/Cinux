/**
 * @file    uart_input.hpp
 * @brief   The serial line as this port's keyboard, feeding event0.
 *
 * QEMU's mcimx6ul-evk speaks input only through what the board really
 * has, and the bring-up lane's honest source is the console UART: every
 * byte the other end types becomes one key event on the neutral
 * EventStream, the same stream the x86 keyboard publishes into. The
 * pointer has no source on this board yet; its seat stays quiet rather
 * than inventing motion.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#pragma once

namespace cinux::driver {

class EventStream;

}  // namespace cinux::driver

namespace cinux::armv7 {

/**
 * @brief         Drains the serial line into both keyboard consumers.
 * @note          One byte source, two consumers: the console queue and
 *                the evdev stream event0 reads. Called from the tick so
 *                a running desktop hears keys even when no console
 *                reader is asking.
 * @since         0.1.0
 * @ingroup       kernel_arch
 */
void PollSerialKeyboard();

/// The stream this port's keyboard publishes into, event0's source.
[[nodiscard]] cinux::driver::EventStream& UartKeyboardEvents();

/// The pointer's seat: real but quiet, no source on this board.
[[nodiscard]] cinux::driver::EventStream& BoardPointerEvents();

/**
 * @brief         Drains the UART receive line into the key stream.
 * @note          Non-blocking: takes whatever waits, publishes one
 *                press and release per byte, closes each with a
 *                report. Call from the tick or console poll path.
 * @since         0.1.0
 * @ingroup kernel_arch
 */

}  // namespace cinux::armv7
