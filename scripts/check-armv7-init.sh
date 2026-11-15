#!/usr/bin/env bash
# The full-boot lane: boot the image behind the init dtb, drip one typed
# probe into the console, and judge the init chain's own words. Same
# runner and same literal discipline as the station lane's arm_boot.

set -uo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build="${CINUX_ARM_BUILD:-$root/build-armv7}"
run_seconds="${CINUX_ARM_RUN_SECONDS:-55}"
qemu="${CINUX_ARM_QEMU:-qemu-system-arm}"

cmake -S "$root" -B "$build" -G Ninja -DCINUX_ARCHITECTURE=armv7 >/dev/null || exit 1
cmake --build "$build" --target arm_image arm_sd_image >/dev/null || exit 1

# The GUI door joins when the desk artifact is there: init respawns it
# from the card's inittab, and the judgement listens for its own words.
# The board has one USB port, so the tablet replaces the keyboard on the
# command line and the typed probe rides the UART instead.
desk_bin=""
for candidate in "$root/build/armv7-vesper/desk" "$build/armv7-vesper/desk"; do
    if [ -f "$candidate" ]; then desk_bin="$candidate"; break; fi
done
extra=()
expect=()
if [ -n "$desk_bin" ]; then
    extra=("-DUSB_DEVICES=-device usb-tablet")
    expect=(-DEXPECTED_DESK_OPEN='vesper-fbdev: fb 640x480 (frame 640x480), kbd /dev/event0, pointer /dev/event1'
            -DEXPECTED_DESK_RUN='vesper-fbdev: running')
fi

exec cmake -DTYPED=1 \
    "-DQEMU_PROGRAM=$qemu" \
    "-DIMAGE_FILE=$build/kernel/arch/armv7/arm.elf" \
    "-DRUN_SECONDS=$run_seconds" \
    "-DDEVICE_FILE=$build/armv7-init.dtb" \
    "-DDEVICE_ADDR=0x81000000" \
    "-DSD_FILE=$build/armv7-sd.img" \
    "-DEXPECTED_TAKEOVER=init lane: kernel_init takes over" \
    "-DEXPECTED_IRQS=init lane: irqs on, init thread starts" \
    "-DEXPECTED_PING=kernel_init: ping 10.0.2.2 answered 0 of 4, launching shell" \
    "-DEXPECTED_SYSINIT=Cinux up" \
    "-DEXPECTED_SHELL_ECHO=armv7-shell-alive" \
    "-DEXPECTED_ADOPTED=file world seated from the card, sh answers through it" \
    "-DSTDIN_FILE=$root/assets/armv7_init_stdin.txt" \
    "-DSTDIN_DELAY=20" \
    "-DSTDIN_DELAY2=15" \
    "-DSTDIN_DELAY3=20" \
    "${extra[@]}" \
    "${expect[@]}" \
    "-DSERIAL_LOG=$build/serial-init.log" \
    -P "$root/cmake/qemu/armv7/boot_check.cmake"
