#!/usr/bin/env bash
# Bakes the armv7 bring-up SD card: an eight-megabyte raw image with an
# MBR (boot signature plus one Linux partition at sector 2048) and a
# minimal ext2 volume inside that partition, seeded with one motd the
# guest can read back through the whole block street. Deterministic in
# shape: same partition table, same file names, every build.
set -euo pipefail

image=$1
script_dir="$(cd "$(dirname "$0")" && pwd)"
seed_dir="${script_dir}/ext2_seed"

partition_start=2048
partition_sectors=14336

# The volume bakes alone first, then lands at its partition's offset.
volume="$(mktemp)"
trap 'rm -f "${volume}"' EXIT
truncate -s $((partition_sectors * 512)) "${volume}"
mkfs.ext2 -q -F -b 1024 -O none -N 64 "${volume}"
debugfs -w -R 'mkdir /etc' "${volume}" >/dev/null 2>&1
debugfs -w -R "write ${seed_dir}/motd.txt /etc/motd" "${volume}" >/dev/null 2>&1
disk_prog="${CINUX_DISK_PROG:-${script_dir}/../assets/armv7_diskprog.elf}"
debugfs -w -R "write ${disk_prog} /prog" "${volume}" >/dev/null 2>&1

# The busybox station's binary rides along the same way: it is a build
# artifact, not tree content, so the volume takes it only when present.
busybox_bin="${CINUX_ARM_BUSYBOX:-}"
if [[ -z "${busybox_bin}" ]]; then
    for candidate in "${script_dir}/../build/armv7-busybox/busybox" \
                     "${script_dir}/../build-armv7/armv7-busybox/busybox"; do
        if [[ -f "${candidate}" ]]; then
            busybox_bin="${candidate}"
            break
        fi
    done
fi
if [[ -n "${busybox_bin}" && -f "${busybox_bin}" ]]; then
    debugfs -w -R "write ${busybox_bin} /bb" "${volume}" >/dev/null 2>&1
    echo "[armv7-sd] busybox seeded at /bb"
fi

# The desktop station's binary: cross-built vesper fbdev desk, a build
# artifact like the others.
desk_bin="${CINUX_ARM_DESK:-}"
if [[ -z "${desk_bin}" ]]; then
    for candidate in "${script_dir}/../build/armv7-vesper/desk" \
                     "${script_dir}/../build-armv7/armv7-vesper/desk"; do
        if [[ -f "${candidate}" ]]; then
            desk_bin="${candidate}"
            break
        fi
    done
fi
if [[ -n "${desk_bin}" && -f "${desk_bin}" ]]; then
    debugfs -w -R "write ${desk_bin} /desk" "${volume}" >/dev/null 2>&1
    echo "[armv7-sd] vesper desk seeded at /desk"
fi

# The glibc station's program rides along only when its image exists:
# building it needs the arm-none-linux-gnueabihf toolchain, which is a
# host-side concern, not a lane requirement. The check stays green with
# or without the artifact.
glibc_prog="${CINUX_GLIBC_PROG:-${script_dir}/../assets/armv7_glibc_hello.elf}"
if [[ -f "${glibc_prog}" ]]; then
    debugfs -w -R "write ${glibc_prog} /glibc" "${volume}" >/dev/null 2>&1
    echo "[armv7-sd] glibc program seeded at /glibc"
fi

# The disk: zeros, the volume at its offset, then the MBR on top.
truncate -s 8M "${image}"
dd if="${volume}" of="${image}" bs=512 seek="${partition_start}" conv=notrunc status=none
printf '\x55\xAA' | dd of="${image}" bs=1 seek=510 conv=notrunc status=none
printf 'CINUXSDMK' | dd of="${image}" bs=1 seek=512 conv=notrunc status=none
printf '\x83' | dd of="${image}" bs=1 seek=450 conv=notrunc status=none
# The table counts little-endian: emit the four bytes of each word
# from its least significant end.
printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' \
    $((partition_start & 0xff)) $(((partition_start >> 8) & 0xff)) \
    $(((partition_start >> 16) & 0xff)) $(((partition_start >> 24) & 0xff)))" \
    | dd of="${image}" bs=1 seek=454 conv=notrunc status=none
printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' \
    $((partition_sectors & 0xff)) $(((partition_sectors >> 8) & 0xff)) \
    $(((partition_sectors >> 16) & 0xff)) $(((partition_sectors >> 24) & 0xff)))" \
    | dd of="${image}" bs=1 seek=458 conv=notrunc status=none

echo "[armv7-sd] molded ${image}: ext2 volume at sector ${partition_start}, motd seeded"
