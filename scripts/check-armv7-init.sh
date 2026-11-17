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
cmake --build "$build" --target arm_image buildroot_volume >/dev/null || exit 1

exec cmake -DTYPED=1 \
    "-DQEMU_PROGRAM=$qemu" \
    "-DIMAGE_FILE=$build/kernel/arch/armv7/arm.elf" \
    "-DRUN_SECONDS=$run_seconds" \
    "-DDEVICE_FILE=$build/armv7-init.dtb" \
    "-DDEVICE_ADDR=0x81000000" \
    "-DSD_FILE=$build/boot/native-build.ext2" \
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
    "-DSERIAL_LOG=$build/serial-init.log" \
    -P "$root/cmake/qemu/armv7/boot_check.cmake"
