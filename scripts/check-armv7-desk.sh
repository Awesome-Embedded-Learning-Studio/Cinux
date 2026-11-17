#!/usr/bin/env bash
# Boots the armv7 lane with the vesper desktop as the guest's last act,
# clicks the TERMINAL icon, types a command into it, and judges the run
# three ways: the desk's own lines, the shell's answer through the pty,
# and the pixels: the terminal's text has to be on the screen.
#
# This is the GUI lane rather than the default check: the desk never
# exits, so the run is judged from the serial log and a screenshot after
# the timeout instead of waiting for a guest verdict.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "${here}/.." && pwd)"
build="${CINUX_ARM_BUILD:-${repo}/build-armv7}"
image="${CINUX_ARM_DESK_IMG:-${build}/boot/native-build.ext2}"
shot="${CINUX_ARM_DESK_SHOT:-${build}/armv7-desk.ppm}"
shot2="${CINUX_ARM_DESK_PAIR_SHOT:-${build}/armv7-desk-pair.ppm}"
shot3="${CINUX_ARM_DESK_CLOSED_SHOT:-${build}/armv7-desk-closed.ppm}"
icon_x="${CINUX_ARM_ICON_X:-40}"
icon_y="${CINUX_ARM_ICON_Y:-110}"
settle="${CINUX_ARM_DESK_DELAY:-24}"
# A pipeline, not a single word: the shell's fork, pipe, exec and
# redirect all have to survive for its answer to reach the screen.
command_text="${CINUX_ARM_DESK_TEXT:-ls / | head -2}"
seconds="${CINUX_ARM_RUN_SECONDS:-50}"

echo '== armv7 desk lane =='
cmake -S "${repo}" -B "${build}" >/dev/null
# The desk and the volume both ride the build graph: the overlay stage
# copies the desk onto the buildroot card, and the GUI trace dtb says
# CINUX_SESSION=gui-trace so the card's desktop script lights the desk
# with its input and pty trace on the serial line.
build_log="${build}/check-armv7-desk.build.log"
if ! cmake --build "${build}" --target vesper_desk buildroot_volume arm_init_gui_trace_dtb \
        >"${build_log}" 2>&1; then
    echo "[check-armv7-desk] build failed, tail of ${build_log}:" >&2
    tail -n 40 "${build_log}" >&2
    exit 1
fi

log="${CINUX_ARM_DESK_LOG:-$(mktemp /tmp/cinux-armv7-desk.XXXXXX.log)}"
qmp="$(mktemp -d /tmp/cinux-desk-qmp.XXXXXX)/qmp"
# QMP pipes need no socket bind permission in a restricted runner.
mkfifo "${qmp}.in" "${qmp}.out"
# One exit door for both duties: the lane restores the default art, and
# a scratch log goes away. A caller who named the log keeps it.
finish() {
    if [ -z "${CINUX_ARM_DESK_LOG:-}" ]; then
        rm -f "${log}"
    fi
    if [ -n "${qmp_reader:-}" ]; then kill "${qmp_reader}" 2>/dev/null || true; fi
    rm -f "${qmp}.in" "${qmp}.out"
    rmdir "$(dirname "${qmp}")"
}
trap finish EXIT
rm -f "${shot}" "${shot2}" "${shot3}"
cat "${qmp}.out" >/dev/null &
qmp_reader=$!
# The keys go down the serial line after the desktop is up, so the
# terminal already owns the keyboard when they land.
# The UART's receive FIFO is thirty-two bytes deep and QEMU hands a
# chardev write to it in one piece, so a burst longer than the FIFO
# loses its tail before any interrupt can run. A real line delivers the
# same text a character at a time; the ten-millisecond gaps reproduce
# that and let the receive interrupt drain each byte as it lands.
type_text() {
    printf '%s' "$1" | sed 's/./&\n/g' | while IFS= read -r kChar; do
        printf '%s' "${kChar}"
        sleep 0.01
    done
    printf '\r'
}
( sleep "${settle}"; type_text "${command_text}"; sleep 12 ) |
    timeout "$((settle + seconds))" "$(command -v qemu-system-arm)" \
        -M mcimx6ul-evk \
        -kernel "${build}/kernel/arch/armv7/arm.elf" \
        -device "loader,file=${build}/armv7-init-gui-trace.dtb,addr=0x81000000,force-raw=on" \
        -drive "file=${image},if=sd,format=raw" \
        -display none -serial stdio -monitor none -qmp "pipe:${qmp}" \
        -device usb-kbd,bus=usb-bus.0 -device usb-tablet,bus=usb-bus.1 \
        > "${log}" 2>/dev/null &
qemu_pid=$!
for _ in $(seq 1 $((settle + 12))); do
    sleep 1
    grep -aq 'vesper-fbdev: running' "${log}" 2>/dev/null && break
done
sleep 1
# The tablet speaks absolute coordinates, so the click names the icon's
# pixel directly, and a double click has to land inside the desk's own
# window: two pairs a couple of hundred ms apart.
(
    printf '%s\n' '{"execute":"qmp_capabilities"}'
    sleep 1
    printf '%s\n' "{\"execute\":\"input-send-event\",\"arguments\":{\"events\":[{\"type\":\"abs\",\"data\":{\"axis\":\"x\",\"value\":$((icon_x * 32767 / 640))}},{\"type\":\"abs\",\"data\":{\"axis\":\"y\",\"value\":$((icon_y * 32767 / 480))}}]}}"
    sleep 1
    for _ in 1 2; do
        printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}'
        sleep 0.2
        printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}'
        sleep 0.2
    done
    sleep "${settle}"
    # Drag the title bar by (120,60): the window has to follow, and the
    # pixels have to say so.
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"abs","data":{"axis":"x","value":5120}},{"type":"abs","data":{"axis":"y","value":3277}}]}}'
    sleep 1
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}'
    sleep 1
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"abs","data":{"axis":"x","value":11264}},{"type":"abs","data":{"axis":"y","value":7373}}]}}'
    sleep 1
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}'
    sleep 3
    printf '%s\n' "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"${shot}\"}}"
    sleep 1
    # A second window: the WELCOME icon opens its About panel while the
    # terminal stays open, so the screen has to carry both at once.
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"abs","data":{"axis":"x","value":2048}},{"type":"abs","data":{"axis":"y","value":2731}}]}}'
    sleep 1
    for _ in 1 2; do
        printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}'
        sleep 0.2
        printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}'
        sleep 0.2
    done
    sleep 3
    printf '%s\n' "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"${shot2}\"}}"
    sleep 1
    # Close the About window with its title bar's X: the gold bar has to
    # leave the screen, and the terminal has to stay.
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"abs","data":{"axis":"x","value":21605}},{"type":"abs","data":{"axis":"y","value":3891}}]}}'
    sleep 1
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}'
    sleep 0.2
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}'
    sleep 3
    printf '%s\n' "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"${shot3}\"}}"
    sleep 1
) > "${qmp}.in" || true
wait "${qemu_pid}" 2>/dev/null || true

failed=0
judge() {
    if grep -aqF "$2" "${log}"; then
        printf '  %s: present\n' "$1"
    else
        printf '  %s: MISSING (%s)\n' "$1" "$2" >&2
        failed=1
    fi
}
judge 'desk opened the panel' 'vesper-fbdev: fb 640x480 (frame 640x480), kbd /dev/event0, pointer /dev/event1'
judge 'desk entered its loop' 'vesper-fbdev: running'
judge 'click reached the icon' "pdown 1 ${icon_x} ${icon_y}"
if grep -aq 'pty=1' "${log}"; then
    printf '  shell answered through the pty: present\n'
else
    printf '  shell answered through the pty: MISSING\n' >&2
    failed=1
fi

# The pixels are the last witness: after the drag the terminal's ink has
# to sit in the window's new box, and the box it left has to show the
# desk's ground instead. The window frame moves by the drag's own delta.
if [ -f "${shot}" ]; then
    if verdict="$(python3 "${here}/count-terminal-ink.py" "${shot}")"; then
        printf '%s\n' "${verdict}" | sed 's/^/  /'
        printf '  terminal text on screen after the drag: present\n'
    else
        printf '%s\n' "${verdict}" | sed 's/^/  /'
        printf '  terminal text on screen after the drag: MISSING\n' >&2
        failed=1
    fi
    # The drag has to have moved the window: the box it lands in no
    # longer starts at the window's opening place.
    box_left="$(printf '%s\n' "${verdict}" | sed -n 's/.*terminal box: \([0-9]*\),\([0-9]*\).*/\1/p')"
    if [ -n "${box_left}" ] && [ "${box_left}" -gt 40 ]; then
        printf '  the window moved with the drag: present (left %s)\n' "${box_left}"
    else
        printf '  the window did not move: left %s\n' "${box_left:-none}" >&2
        failed=1
    fi
    # Two windows at once: the About frame's title bar is the theme's
    # gold, at its own fixed place, beside the terminal's dark box.
    if bar="$(python3 "${here}/count-terminal-ink.py" "${shot2}" \
              --box 162,52,428,64 --color 201,138,45)"; then
        printf '%s\n' "${bar}" | sed 's/^/  /'
        printf '  a second window beside the terminal: present\n'
    else
        printf '%s\n' "${bar}" | sed 's/^/  /'
        printf '  a second window beside the terminal: MISSING\n' >&2
        failed=1
    fi
    # And the close button has to take it away again, leaving the terminal
    # behind: the same gold box now empty, the dark box still carrying text.
    if gone="$(python3 "${here}/count-terminal-ink.py" "${shot3}" \
               --box 162,52,428,64 --color 201,138,45)"; then
        printf '%s\n' "${gone}" | sed 's/^/  /'
        printf '  the closed window is gone: MISSING (its bar is still there)\n' >&2
        failed=1
    else
        printf '%s\n' "${gone}" | sed 's/^/  /'
        printf '  the closed window is gone: present\n'
    fi
    if python3 "${here}/count-terminal-ink.py" "${shot3}" >/dev/null 2>&1; then
        printf '  the terminal survived the close: present\n'
    else
        printf '  the terminal survived the close: MISSING\n' >&2
        failed=1
    fi
else
    printf '  screenshot: MISSING (%s)\n' "${shot}" >&2
    failed=1
fi

if [ "${failed}" -ne 0 ]; then
    echo "[armv7-desk] FAILED; serial log follows:" >&2
    tail -40 "${log}" >&2
    exit 1
fi
echo '[armv7-desk] ok: the desktop runs, the terminal opens and its text is drawn'
