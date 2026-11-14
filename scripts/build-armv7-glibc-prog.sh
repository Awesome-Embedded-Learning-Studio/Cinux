#!/usr/bin/env bash
# Builds the glibc station's armv7 program: a statically linked ELF the
# SD image bakes as /glibc, which the disk program execs into. The
# result caches at assets/armv7_glibc_hello.elf; remove it to rebuild.
#
# The cross compiler is a host concern, so the script looks for it in
# the usual places and says plainly what it needs when none is found:
#   - CINUX_ARM_CROSS pointing at an arm-*-linux-gnueabihf-gcc
#   - an arm-none-linux-gnueabihf-gcc on PATH
#   - a Debian/Ubuntu cross tree unpacked under ~/.local/opt/arm-cross
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "${here}/.." && pwd)"
out="${1:-${repo}/assets/armv7_glibc_hello.elf}"
source_file="${repo}/assets/armv7_glibc_hello.c"

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
    cat >&2 <<'EOF'
[build-armv7-glibc-prog] no armhf glibc cross compiler found.
Install one and point CINUX_ARM_CROSS at it, for example:
  - Arm's prebuilt toolchain (arm-none-linux-gnueabihf-gcc), or
  - Debian/Ubuntu's gcc-arm-linux-gnueabihf + libc6-dev-armhf-cross,
    unpacked with `ar x <deb> && tar xf data.tar.*` under
    ~/.local/opt/arm-cross
The lane itself does not need this: without the artifact the image
simply carries no /glibc and the check skips its judgement.
EOF
    exit 1
fi

# The unpacked Debian tree carries its own binutils libraries beside
# the compiler; the host's own paths stay in front of nothing here.
case "${cross}" in
    "${cross_tree}"/*)
        export LD_LIBRARY_PATH="${cross_tree}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
        ;;
esac

fingerprint="$({ sha256sum "${source_file}" "${BASH_SOURCE[0]}";
                 "${cross}" --version; "${cross}" -dumpmachine;
                 printf 'desk=%s\n' "${CINUX_ARM_PROBE_DESK:-}"; } | sha256sum | cut -d ' ' -f 1)"
if [ -f "${out}" ] && [ -f "${out}.inputs" ] && \
   [ "$(cat "${out}.inputs")" = "${fingerprint}" ]; then
    echo "[build-armv7-glibc-prog] cache hit, keeping ${out}"
    exit 0
fi

mkdir -p "$(dirname "${out}")"
"${cross}" \
    -mcpu=cortex-a7 -marm -mfpu=neon-vfpv4 -mfloat-abi=hard \
    -static -no-pie -fno-pie -O2 \
    ${CINUX_ARM_PROBE_DESK:+-DCINUX_ARM_PROBE_DESK=1} \
    -Wl,-Ttext-segment=0x40010000 \
    -o "${out}.part" "${source_file}"
mv "${out}.part" "${out}"
printf '%s\n' "${fingerprint}" > "${out}.inputs"
echo "[build-armv7-glibc-prog] built ${out}"
