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
image="${CINUX_ARM_DESK_IMG:-${build}/armv7-desk.img}"
shot="${CINUX_ARM_DESK_SHOT:-${build}/armv7-desk.ppm}"
icon_x="${CINUX_ARM_ICON_X:-40}"
icon_y="${CINUX_ARM_ICON_Y:-110}"
settle="${CINUX_ARM_DESK_DELAY:-16}"
command_text="${CINUX_ARM_DESK_TEXT:-echo hi}"
seconds="${CINUX_ARM_RUN_SECONDS:-34}"

echo '== armv7 desk lane =='
cmake -S "${repo}" -B "${build}" >/dev/null
CINUX_ARM_PROBE_DESK=1 "${here}/build-armv7-glibc-prog.sh" >/dev/null
trap '"${here}/build-armv7-glibc-prog.sh" >/dev/null 2>&1 || true' EXIT
"${here}/build-armv7-vesper-desk.sh" >/dev/null
# The image bakes whatever the artifacts currently hold: mold a desk
# image of its own so the default lane's card stays untouched.
CINUX_ARM_SD_IMAGE="${image}" cmake --build "${build}" --target arm_image >/dev/null
rm -f "${image}"
/bin/sh "${here}/create_armv7_sd.sh" "${image}" >/dev/null

log="$(mktemp /tmp/cinux-armv7-desk.XXXXXX.log)"
qmp="$(mktemp -u /tmp/cinux-desk-qmp.XXXXXX.sock)"
trap 'rm -f "${log}" "${qmp}"' EXIT
rm -f "${shot}"
# The keys go down the serial line after the desktop is up, so the
# terminal already owns the keyboard when they land.
( sleep 8; cat "${repo}/assets/armv7_stdin.txt"; sleep "${settle}"; printf '%s\r' "${command_text}"; sleep 12 ) |
    timeout "$((settle + seconds))" "$(command -v qemu-system-arm)" \
        -M mcimx6ul-evk \
        -kernel "${build}/kernel/arch/armv7/arm.elf" \
        -device "loader,file=${build}/armv7-qemu.dtb,addr=0x81000000,force-raw=on" \
        -drive "file=${image},if=sd,format=raw" \
        -display none -serial stdio -monitor none -qmp "unix:${qmp},server,nowait" \
        -device usb-tablet \
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
    printf '%s\n' "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"${shot}\"}}"
    sleep 1
) | socat - "UNIX-CONNECT:${qmp}" >/dev/null 2>&1 || true
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

# The pixels are the last witness: the terminal's ink has to be on the
# screen inside the window the click opened.
if [ -f "${shot}" ]; then
    if python3 "${here}/count-terminal-ink.py" "${shot}"; then
        printf '  terminal text on screen: present\n'
    else
        printf '  terminal text on screen: MISSING\n' >&2
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
