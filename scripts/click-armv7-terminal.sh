#!/usr/bin/env bash
# Clicks the armv7 desktop's TERMINAL icon and types into it: the tablet
# carries the click (absolute coordinates over QMP), the serial line
# carries the keys, and the desktop answers with its own trace lines.
#
# A clicked terminal takes focus, so the keys land in the pty and the
# shell echoes them back: the run judges that by the pty= word the desk
# prints once its loop sees the shell's answer.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "${here}/.." && pwd)"
build="${CINUX_ARM_BUILD:-${repo}/build-armv7}"
image="${CINUX_ARM_TERM_IMG:-${build}/armv7-term.img}"
icon_x="${CINUX_ARM_ICON_X:-40}"
icon_y="${CINUX_ARM_ICON_Y:-110}"
settle="${CINUX_ARM_TERM_DELAY:-18}"
text="${1:-ls -l /\r}"
shot="${CINUX_ARM_SHOT:-${build}/armv7-terminal.ppm}"

echo '== armv7 terminal lane =='
cmake -S "${repo}" -B "${build}" >/dev/null
CINUX_ARM_PROBE_DESK=1 "${here}/build-armv7-glibc-prog.sh" >/dev/null
trap '"${here}/build-armv7-glibc-prog.sh" >/dev/null 2>&1 || true' EXIT
"${here}/build-armv7-vesper-desk.sh" >/dev/null
CINUX_ARM_SD_IMAGE="${image}" cmake --build "${build}" --target arm_image >/dev/null
rm -f "${image}"
/bin/sh "${here}/create_armv7_sd.sh" "${image}" >/dev/null

log="$(mktemp /tmp/cinux-armv7-term.XXXXXX.log)"
trap 'rm -f "${log}"' EXIT
qmp="$(mktemp -u /tmp/cinux-qmp.XXXXXX.sock)"
( sleep 8; cat "${repo}/assets/armv7_stdin.txt"; sleep "${settle}"; printf '%b' "${text}"; sleep 6 ) |
    timeout "$((settle + 26))" "$(command -v qemu-system-arm)" \
        -M mcimx6ul-evk \
        -kernel "${build}/kernel/arch/armv7/arm.elf" \
        -device "loader,file=${build}/armv7-qemu.dtb,addr=0x81000000,force-raw=on" \
        -drive "file=${image},if=sd,format=raw" \
        -display none -serial stdio -monitor none -qmp "unix:${qmp},server,nowait" \
        -device usb-tablet \
        > "${log}" 2>/dev/null &
qemu_pid=$!
for _ in $(seq 1 34); do
    sleep 1
    grep -aq 'vesper-fbdev: running' "${log}" 2>/dev/null && break
done
sleep 2
# The tablet speaks absolute coordinates, so the click names the icon's
# pixel directly: 640x480 maps onto the tablet's 0..32767 span.
# One connection for the whole exchange: QMP negotiates capabilities
# per connection, so a second socat would be refused.
(
    printf '%s\n' '{"execute":"qmp_capabilities"}'
    sleep 1
    printf '%s\n' "{\"execute\":\"input-send-event\",\"arguments\":{\"events\":[{\"type\":\"abs\",\"data\":{\"axis\":\"x\",\"value\":$((icon_x * 32767 / 640))}},{\"type\":\"abs\",\"data\":{\"axis\":\"y\",\"value\":$((icon_y * 32767 / 480))}}]}}"
    sleep 1
    # The icon opens on a double click: two press/release pairs inside
    # the desk's click window.
    # The desk's clock is real now, so a double click has to land inside
    # its window: two press/release pairs a couple of hundred ms apart.
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}'
    sleep 0.2
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}'
    sleep 0.2
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":true,"button":"left"}}]}}'
    sleep 0.2
    printf '%s\n' '{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":false,"button":"left"}}]}}'
    sleep 4
    printf '%s\n' "{\"execute\":\"screendump\",\"arguments\":{\"filename\":\"${shot}\"}}"
    sleep 1
) | socat - "UNIX-CONNECT:${qmp}" >/dev/null 2>&1 || true
wait "${qemu_pid}" 2>/dev/null || true
rm -f "${qmp}"

clicked=0
grep -aq "pdown 1 ${icon_x} ${icon_y}" "${log}" && clicked=1
echo "  click at ${icon_x},${icon_y}: $([ "${clicked}" = 1 ] && echo delivered || echo MISSING)"
if grep -aq 'pty=1' "${log}"; then
    echo '  shell: answered through the pty'
else
    echo '  shell: quiet (no pty word in the trace)'
fi
echo "  screenshot: ${shot}"
