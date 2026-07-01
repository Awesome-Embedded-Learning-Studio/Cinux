#!/usr/bin/env bash
#
# build-busybox.sh -- build a static BusyBox for the CinuxOS ecosystem smoke.
#
# Produces build/musl/busybox by default.  The config intentionally enables only
# the applets exercised by the kernel smoke plus the interactive GUI shell path.
set -euo pipefail

BUSYBOX_VER="${BUSYBOX_VER:-1.36.1}"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SYSROOT="${MUSL_SYSROOT:-$REPO/build/musl-sysroot}"
WORK="$REPO/build/musl"
SRC="$WORK/busybox-$BUSYBOX_VER"
TARBALL="$WORK/busybox-$BUSYBOX_VER.tar.bz2"
OUT="${1:-$REPO/build/musl/busybox}"
CC="${BUSYBOX_CC:-$SYSROOT/bin/musl-gcc}"

if [ -x "$OUT" ]; then
    echo "[build-busybox] busybox already present at $OUT (rm to rebuild)"
    exit 0
fi

if [ ! -x "$CC" ]; then
    echo "[build-busybox] compiler missing: $CC" >&2
    echo "[build-busybox] run tools/musl/build-musl.sh first" >&2
    exit 1
fi

mkdir -p "$WORK" "$(dirname "$OUT")"

if [ ! -f "$TARBALL" ]; then
    echo "[build-busybox] downloading busybox-$BUSYBOX_VER..."
    curl -fsSL "https://busybox.net/downloads/busybox-$BUSYBOX_VER.tar.bz2" -o "$TARBALL"
fi

if [ ! -d "$SRC" ]; then
    echo "[build-busybox] extracting..."
    tar -C "$WORK" -xjf "$TARBALL"
fi

cd "$SRC"
echo "[build-busybox] configuring minimal static applet set..."
make allnoconfig >/dev/null

enable() {
    ./scripts/config -e "$1"
}

set_val() {
    ./scripts/config --set-val "$1" "$2"
}

enable STATIC
enable FEATURE_PREFER_APPLETS
enable BUSYBOX

# Shell path: /bin/sh is the busybox binary in the ext2 image, so "sh" must
# dispatch to ash.  Editing support is useful for the GUI PTY shell.
enable ASH
enable SH_IS_ASH
enable FEATURE_EDITING
enable FEATURE_EDITING_SAVEHISTORY
enable FEATURE_TAB_COMPLETION
enable FEATURE_EDITING_FANCY_PROMPT
set_val FEATURE_EDITING_MAX_LEN 1024
set_val FEATURE_EDITING_HISTORY 255

# Applets installed as /bin hard links by scripts/create_ext2_disk.sh and used by
# the CINUX_BUSYBOX_SMOKE acceptance batch.
for applet in \
    LS CLEAR CAT ECHO PWD UNAME ID WHOAMI TRUE FALSE SLEEP ENV HOSTNAME WC FREE PS \
    MKDIR RMDIR TOUCH LN CHMOD CHOWN RM CP MV READLINK
do
    enable "$applet"
done

if ! make -s olddefconfig; then
    make -s oldconfig </dev/null
fi

echo "[build-busybox] building with $(nproc) jobs..."
make -j"$(nproc)" CC="$CC" busybox
install -m 0755 busybox "$OUT"

echo "[build-busybox] artifact:"
file "$OUT"
