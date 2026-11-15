#!/usr/bin/env bash
# Rebuilds assets/armv7_userprog.elf, the seated probe the armv7 lane embeds.
#
# The image build copies this ELF into the kernel as a blob; it is a build
# product (gitignored), so a change under assets/armv7_userprog.c only takes
# effect after this script runs. Link address, entry point and section
# placement come from assets/armv7_userprog.ld, which also parks .data/.bss
# at 0x40002000 so the probe's writable bytes never share a page with its
# code.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "${here}/.." && pwd)"
compiler="${CINUX_ARM_PROBE_GCC:-arm-none-eabi-gcc}"

if ! command -v "${compiler}" >/dev/null 2>&1; then
    echo "[build-armv7-probe] no ${compiler} on PATH" >&2
    exit 1
fi

"${compiler}" -ffreestanding -nostdlib -fno-pic -fno-builtin -O2 \
    -march=armv7-a -marm \
    -T "${repo}/assets/armv7_userprog.ld" -e guest_main \
    -o "${repo}/assets/armv7_userprog.elf" \
    "${repo}/assets/armv7_userprog.c"

echo "[build-armv7-probe] built $(basename "${repo}")/assets/armv7_userprog.elf"
