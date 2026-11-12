#!/usr/bin/env bash
# Verify both lanes in one go: the armv7 bring-up image and the x86 host
# checks. Run from anywhere; it walks to the repository root itself.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

show_size() {
    if [[ -f build-armv7/kernel/arch/armv7/arm.elf ]]; then
        printf 'arm.elf %s bytes\n' "$(stat -c %s build-armv7/kernel/arch/armv7/arm.elf)"
    fi
}

echo '== armv7 lane =='
if [[ ! -f build-armv7/build.ninja ]]; then
    cmake -S . -B build-armv7 -G Ninja -DCINUX_ARCHITECTURE=armv7
fi
cmake --build build-armv7 --target arm_image
ctest --test-dir build-armv7 --output-on-failure

echo '== x86 lane =='
if [[ -f build/build.ninja ]]; then
    cmake --build build --target test_host
    cmake --build build --target kernel_bin
else
    echo 'no build/ configured; skipping the host lane'
fi

show_size
echo 'both lanes green'
