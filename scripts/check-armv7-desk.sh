#!/usr/bin/env bash
# Boots the armv7 lane with the vesper desktop as the guest's last act
# and judges the lines that mean it is really running: the panel and
# input doors opened, the spawn found a seat, and the desk loop began.
#
# This is a looking-at-it lane, not the default check: the desk never
# exits, so the run is judged from the serial log after the timeout
# instead of waiting for a guest verdict.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "${here}/.." && pwd)"
build="${CINUX_ARM_BUILD:-${repo}/build-armv7}"
image="${CINUX_ARM_DESK_IMG:-${build}/armv7-desk.img}"
seconds="${CINUX_ARM_RUN_SECONDS:-30}"

echo '== armv7 desk lane =='
cmake -S "${repo}" -B "${build}" >/dev/null
# The probe is one artifact shared with the default lane: build the desk
# variant, and put the check variant back on the way out so a later boot
# mints the judged card again.
CINUX_ARM_PROBE_DESK=1 "${here}/build-armv7-glibc-prog.sh" >/dev/null
trap '"${here}/build-armv7-glibc-prog.sh" >/dev/null 2>&1 || true' EXIT
"${here}/build-armv7-vesper-desk.sh" >/dev/null
# The image bakes whatever the artifacts currently hold: mold a desk
# image of its own so the default lane's card stays untouched.
CINUX_ARM_SD_IMAGE="${image}" cmake --build "${build}" --target arm_image >/dev/null
rm -f "${image}"
/bin/sh "${here}/create_armv7_sd.sh" "${image}" >/dev/null

log="$(mktemp /tmp/cinux-armv7-desk.XXXXXX.log)"
trap 'rm -f "${log}"' EXIT
( sleep 8; cat "${repo}/assets/armv7_stdin.txt"; sleep "${seconds}" ) |
    timeout "$((seconds + 4))" "$(command -v qemu-system-arm)" \
        -M mcimx6ul-evk \
        -kernel "${build}/kernel/arch/armv7/arm.elf" \
        -device "loader,file=${build}/armv7-qemu.dtb,addr=0x81000000,force-raw=on" \
        -drive "file=${image},if=sd,format=raw" \
        -display none -serial stdio -monitor none -device usb-kbd \
        > "${log}" 2>/dev/null || true

failed=0
judge() {
    if grep -aqF "$2" "${log}"; then
        printf '  %s: present\n' "$1"
    else
        printf '  %s: MISSING (%s)\n' "$1" "$2" >&2
        failed=1
    fi
}
judge 'desk opened the panel' 'vesper-fbdev: fb 640x480'
judge 'desk seated its keyboard' 'vesper-fbdev: fb 640x480 (frame 640x480), kbd /dev/event0, pointer /dev/event1'
judge 'desk entered its loop' 'vesper-fbdev: running'

if [ "${failed}" -ne 0 ]; then
    echo "[armv7-desk] FAILED; serial log follows:" >&2
    tail -40 "${log}" >&2
    exit 1
fi
echo '[armv7-desk] ok: the desktop runs on the port'
