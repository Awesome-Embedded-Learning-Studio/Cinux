#!/bin/bash
# Assemble the gcc-profile rootfs.ext2: buildroot base target + GCC toolchain
# closure (extract.sh) + overlay, packed with mkfs.ext2 -d.  The result ships a
# native gcc driver so `gcc -fno-pie -no-pie /hello.c` runs on CinuxOS.
#
# Usage: assemble_gcc_rootfs.sh <output_img> [buildroot_target] [gcc_root]
#   buildroot_target : buildroot output/target dir
#                      (default $REPO/build/buildroot/output/target)
#   gcc_root         : extract.sh output dir (default $REPO/build/gcc-root;
#                      recreated on each assemble to avoid stale host closures)
#
# This is the gcc-profile counterpart to the handcrafted create_ext2_disk.sh:
# buildroot builds the base musl/busybox target out-of-tree, and this script
# merges in the host GCC closure (glibc-dynamic) plus the CinuxOS overlay.  The
# two dynamic loaders coexist: /lib64/ld-linux-x86-64.so.2 (gcc/cc1/as/ld) and
# /lib/ld-musl-x86_64.so.1 (busybox).
set -euo pipefail

OUTPUT="${1:?usage: $0 <output_img> [buildroot_target] [gcc_root]}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# BR_TARGET / GCC_ROOT may be overridden via the environment too -- CI uses a
# buildroot output dir different from the stage-1 default (build/buildroot-ci).
BR_TARGET="${2:-${BR_TARGET:-$REPO_ROOT/build/buildroot/output/target}}"
GCC_ROOT="${3:-${GCC_ROOT:-$REPO_ROOT/build/gcc-root}}"

if [ ! -d "$BR_TARGET" ]; then
    echo "[assemble] error: buildroot target dir not found: $BR_TARGET" >&2
    echo "[assemble]        build the base rootfs first (buildroot make)." >&2
    exit 1
fi

# Stage the GCC closure every time.  It is quick, and stale closures are easy to
# hit when iterating locally or under act with a reused build directory.
echo "[assemble] staging GCC closure via extract.sh..."
"$REPO_ROOT/tools/gcc-toolchain/extract.sh" "$GCC_ROOT" >/dev/null

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cp -a "$BR_TARGET" "$WORK/target"

# buildroot target /lib64 is a symlink -> /lib (musl layout); the GCC closure
# ships /lib64 as a real dir (glibc ld-linux-x86-64.so.2).  Drop the symlink so
# the real dir merges in -- both loaders coexist (see header comment).
rm -f "$WORK/target/lib64"
cp -a "$GCC_ROOT/." "$WORK/target/"

# The latest overlay (inittab + usability script) wins over the merged tree.
cp "$REPO_ROOT/rootfs/overlay/etc/inittab" "$WORK/target/etc/inittab"
cp "$REPO_ROOT/rootfs/overlay/etc/cinux-usability-test.sh" "$WORK/target/etc/cinux-usability-test.sh"

# 128 MB / 8192 inodes holds base (~5 MB) + GCC closure (~71 MB, ~10k headers).
# block_size=1024 matches the CinuxOS ext2 driver's expectation.
dd if=/dev/zero of="$OUTPUT" bs=1M count=128 status=none
mkfs.ext2 -q -b 1024 -O none -N 8192 -d "$WORK/target" "$OUTPUT"

echo "[assemble] gcc rootfs -> $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
