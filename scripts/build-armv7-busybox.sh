#!/usr/bin/env bash
# Builds the armv7 station's busybox: the same source tree and config the
# x86 lane uses, compiled with the armhf cross compiler and linked into
# the armv7 user window (0x40010000 — the port's program floor), so the
# loader can place it. The output caches at
# build/armv7-busybox/busybox; remove it to rebuild.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "${here}/.." && pwd)"
out="${1:-${repo}/build/armv7-busybox/busybox}"
cross_tree="${HOME}/.local/opt/arm-cross"

find_cross() {
    if [ -n "${CINUX_ARM_CROSS:-}" ]; then
        printf '%s\n' "${CINUX_ARM_CROSS}"
        return 0
    fi
    local name
    for name in arm-none-linux-gnueabihf-gcc arm-linux-gnueabihf-gcc; do
        if command -v "${name}" >/dev/null 2>&1; then
            command -v "${name}"
            return 0
        fi
    done
    local candidate="${cross_tree}/usr/bin/arm-linux-gnueabihf-gcc"
    if [ -x "${candidate}" ]; then
        printf '%s\n' "${candidate}"
        return 0
    fi
    return 1
}

if ! cross="$(find_cross)"; then
    echo "[build-armv7-busybox] no armhf glibc cross compiler found; see build-armv7-glibc-prog.sh" >&2
    exit 1
fi
case "${cross}" in
    "${cross_tree}"/*)
        export LD_LIBRARY_PATH="${cross_tree}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
        ;;
esac
strip="${cross%gcc}strip"

source_tree="${CINUX_BUSYBOX_SOURCE:-}"
if [ -z "$source_tree" ]; then
    version=1.38.0
    checksum=34f9ea6ff8636f2c9241153b9114eefa9e65674a45318ae1ef95bb5f31c53bb2
    cache="${CINUX_ROOTFS_CACHE:-${repo}/.cache/rootfs}"
    archive="${cache}/busybox-${version}.tar.bz2"
    mkdir -p "$cache"
    if [ ! -f "$archive" ]; then
        curl -fL --retry 3 --output "${archive}.part" \
            "https://busybox.net/downloads/busybox-${version}.tar.bz2"
        mv "${archive}.part" "$archive"
    fi
    printf '%s  %s\n' "$checksum" "$archive" | sha256sum -c -
    if [ ! -d "${cache}/busybox-${version}" ]; then
        tar -xjf "$archive" -C "$cache"
    fi
    source_tree="${cache}/busybox-${version}"
fi

fingerprint="$({ sha256sum "${repo}/user/rootfs/busybox/busybox.config" "${BASH_SOURCE[0]}";
                 "${cross}" --version; "${cross}" -dumpmachine;
                 printf '%s\n' "${source_tree}"; } | sha256sum | cut -d ' ' -f 1)"
if [ -f "$out" ] && [ -f "${out}.inputs" ] && \
   [ "$(cat "${out}.inputs")" = "$fingerprint" ]; then
    echo "[build-armv7-busybox] cache hit, keeping ${out}"
    exit 0
fi

work="$(mktemp -d /tmp/cinux-armv7-busybox.XXXXXX)"
trap 'rm -rf "$work"' EXIT
cp -a "$source_tree"/. "$work"/
make -C "$work" clean >/dev/null
cp "${repo}/user/rootfs/busybox/busybox.config" "$work/.config"
python3 - "$work/.config" <<'EOF'
import sys
path = sys.argv[1]
with open(path) as handle:
    lines = handle.readlines()
with open(path, "w") as handle:
    for line in lines:
        if line.startswith("CONFIG_EXTRA_CFLAGS="):
            handle.write('CONFIG_EXTRA_CFLAGS="-fno-pie"\n')
        elif line.startswith("CONFIG_EXTRA_LDFLAGS="):
            # The armv7 user window starts at 0x40000000: a program linked
            # at the default 0x10000 lands under the kernel's identity map
            # and the loader has nowhere to put it.
            handle.write('CONFIG_EXTRA_LDFLAGS="-no-pie -Wl,-Ttext-segment=0x40010000"\n')
        else:
            handle.write(line)
EOF
# busybox strips its own binary through $(STRIP); the host strip cannot
# read an ARM object, so the cross one is named explicitly.
make -C "$work" CC="$cross" HOSTCC=cc STRIP="$strip" -j"${CINUX_ROOTFS_JOBS:-4}" >/dev/null
mkdir -p "$(dirname "$out")"
# The symbol table is worth keeping beside the station: a crash report
# names a pc, and only an unstripped copy can say which function it was.
cp "$work/busybox_unstripped" "${out}.debug" 2>/dev/null || true
"${strip}" "$work/busybox_unstripped" -o "${out}.part"
mv "${out}.part" "$out"
printf '%s\n' "$fingerprint" > "${out}.inputs"
echo "[build-armv7-busybox] built ${out}"
