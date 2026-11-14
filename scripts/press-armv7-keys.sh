#!/usr/bin/env bash
# Boots the armv7 lane with the desktop and types into it: the keys go
# down the same serial line the console uses, so the port's keyboard
# stream (event0) is what carries them to the desktop.
#
# The lane prints whatever the desktop traced, so a key that arrives
# shows up as a "trace[" line. A key that never arrives prints nothing,
# which is itself the answer the run is looking for.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "${here}/.." && pwd)"
build="${CINUX_ARM_BUILD:-${repo}/build-armv7}"
image="${CINUX_ARM_KEY_IMG:-${build}/armv7-keys.img}"
text="${1:-ls -l /}"
settle="${CINUX_ARM_KEY_DELAY:-16}"
seconds="${CINUX_ARM_RUN_SECONDS:-26}"

echo "== armv7 keyboard lane =="
cmake -S "${repo}" -B "${build}" >/dev/null
CINUX_ARM_PROBE_DESK=1 "${here}/build-armv7-glibc-prog.sh" >/dev/null
trap '"${here}/build-armv7-glibc-prog.sh" >/dev/null 2>&1 || true' EXIT
"${here}/build-armv7-vesper-desk.sh" >/dev/null
CINUX_ARM_SD_IMAGE="${image}" cmake --build "${build}" --target arm_image >/dev/null
rm -f "${image}"
/bin/sh "${here}/create_armv7_sd.sh" "${image}" >/dev/null

log="$(mktemp /tmp/cinux-armv7-keys.XXXXXX.log)"
trap 'rm -f "${log}"' EXIT
( sleep 8; cat "${repo}/assets/armv7_stdin.txt"; sleep "${settle}"; printf '%s\r' "${text}"; sleep 6 ) |
    timeout "$((settle + seconds))" "$(command -v qemu-system-arm)" \
        -M mcimx6ul-evk \
        -kernel "${build}/kernel/arch/armv7/arm.elf" \
        -device "loader,file=${build}/armv7-qemu.dtb,addr=0x81000000,force-raw=on" \
        -drive "file=${image},if=sd,format=raw" \
        -display none -serial stdio -monitor none -device usb-tablet \
        > "${log}" 2>/dev/null || true

if grep -aq 'vesper-fbdev: running' "${log}"; then
    echo '  desktop: running'
else
    echo '  desktop: MISSING (the run never reached its loop)' >&2
fi
if grep -aq 'trace\[' "${log}"; then
    echo '  keys: arrived at the desktop'
    grep -a 'trace\[' "${log}" | tail -5 | sed 's/^/    /'
else
    echo '  keys: none reached the desktop'
fi
printf '  typed: %s\n' "${text}"
