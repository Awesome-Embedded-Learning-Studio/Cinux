#!/usr/bin/env bash
# Builds the GUI station's armv7 desk: vesper's fbdev_desktop, statically
# linked against glibc, cross-compiled with the armhf GCC 14 tree the
# desktop sources need (C++23 <print>). The output caches at
# build/armv7-vesper/desk; remove it to rebuild.
#
# The source tree is the same third_party/vesper the x86 lane builds, so
# both lanes run one desktop, not two.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "${here}/.." && pwd)"
out="${1:-${repo}/build/armv7-vesper/desk}"
build="${CINUX_ARM_VESPER_BUILD:-${repo}/build/armv7-vesper/build}"
root="${CINUX_ARM_CROSS_ROOT:-${HOME}/.local/opt/arm-cross14}"

if [ ! -x "${root}/usr/bin/arm-linux-gnueabihf-g++-14" ]; then
    echo "[build-armv7-vesper] no armhf GCC 14 cross tree at ${root}" >&2
    echo "  the desktop sources use C++23 <print>, which GCC 14 introduced" >&2
    exit 1
fi
if [ ! -f "${repo}/third_party/vesper/CMakeLists.txt" ]; then
    echo "[build-armv7-vesper] no desktop repo at third_party/vesper" >&2
    exit 1
fi
export LD_LIBRARY_PATH="${root}/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

fingerprint="$({ sha256sum "${BASH_SOURCE[0]}" "${repo}/cmake/toolchains/armv7-linux-gnueabihf.cmake";
                 cd "${repo}/third_party/vesper" && git rev-parse HEAD 2>/dev/null || true;
                 "${root}/usr/bin/arm-linux-gnueabihf-g++-14" --version; } | sha256sum | cut -d ' ' -f 1)"
if [ -f "${out}" ] && [ -f "${out}.inputs" ] && \
   [ "$(cat "${out}.inputs")" = "${fingerprint}" ]; then
    echo "[build-armv7-vesper] cache hit, keeping ${out}"
    exit 0
fi

cmake -S "${repo}/third_party/vesper" -B "${build}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${repo}/cmake/toolchains/armv7-linux-gnueabihf.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DVESPER_BACKEND=fbdev \
    -DCMAKE_EXE_LINKER_FLAGS="-static -Wl,-Ttext-segment=0x40010000" \
    -DCMAKE_CXX_FLAGS="-static" >/dev/null
cmake --build "${build}" --target fbdev_desktop --parallel "${CINUX_ROOTFS_JOBS:-4}" >/dev/null

mkdir -p "$(dirname "${out}")"
cp "${build}/examples/fbdev_desktop" "${out}"
printf '%s\n' "${fingerprint}" > "${out}.inputs"
echo "[build-armv7-vesper] built $(ls -lh "${out}" | awk '{print $5}') static armv7 desk"
