#!/usr/bin/env bash
# Runs the armv7 desktop for a human: the screen goes to a VNC display
# (point and type there). Ctrl-C in this terminal ends the run.
#
# One boot, one image: the probe on the disk hands over to the desktop,
# which opens the panel, spawns a shell on a pty, and waits for you.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "${here}/.." && pwd)"
build="${CINUX_ARM_BUILD:-${repo}/build-armv7}"
image="${CINUX_ARM_DEMO_IMG:-${build}/armv7-demo.img}"
display="${CINUX_ARM_VNC:-:7}"
seconds="${CINUX_ARM_DEMO_SECONDS:-900}"

echo '== armv7 desktop demo =='
cmake -S "${repo}" -B "${build}" >/dev/null
CINUX_ARM_PROBE_DESK=1 "${here}/build-armv7-glibc-prog.sh" >/dev/null
# The probe is one artifact shared with the default lane: put the judged
# card back on the way out, probe and image both.
restore_default_lane() {
    "${here}/build-armv7-glibc-prog.sh" >/dev/null 2>&1 || true
    cmake --build "${build}" --target arm_sd_image >/dev/null 2>&1 || true
}
trap restore_default_lane EXIT
"${here}/build-armv7-vesper-desk.sh" >/dev/null
CINUX_ARM_SD_IMAGE="${image}" cmake --build "${build}" --target arm_image >/dev/null
rm -f "${image}"
/bin/sh "${here}/create_armv7_sd.sh" "${image}" >/dev/null

cat <<EOF
  VNC display: vncviewer 127.0.0.1:${display#:}  (port $((5900 + ${display#:})))
  keyboard:    the VNC window (USB keyboard on usb-bus.0)
  mouse:       the VNC window (the tablet reports absolute positions)
  Ctrl-C ends the run.
EOF

# Stdio carries the startup probe and logs; VNC carries interactive keys.
( sleep 2; cat "${repo}/assets/armv7_stdin.txt"; sleep "${seconds}" ) |
    timeout "$((seconds + 5))" "$(command -v qemu-system-arm)" \
        -M mcimx6ul-evk \
        -kernel "${build}/kernel/arch/armv7/arm.elf" \
        -device "loader,file=${build}/armv7-qemu.dtb,addr=0x81000000,force-raw=on" \
        -drive "file=${image},if=sd,format=raw" \
        -serial stdio -monitor none -vnc "${display}" \
        -device usb-kbd,bus=usb-bus.0 -device usb-tablet,bus=usb-bus.1 || true
