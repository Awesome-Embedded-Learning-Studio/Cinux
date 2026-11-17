#!/usr/bin/env bash
# Verify both lanes: the armv7 bring-up image and the x86 host checks.
# --repeat N runs the armv7 boot N times, since some of its checks depend
# on how the scheduler interleaves; failures keep their serial log.

set -uo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

repeat=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --repeat)
            repeat=${2:-1}
            shift 2
            ;;
        *)
            echo "usage: $0 [--repeat N]" >&2
            exit 2
            ;;
    esac
done

show_size() {
    local image
    for image in build-armv7/kernel/arch/armv7/arm.elf \
                 build-armv7/kernel/arch/armv7/arm-selftest.elf; do
        if [[ -f "${image}" ]]; then
            printf '%s %s bytes\n' "$(basename "${image}")" "$(stat -c %s "${image}")"
        fi
    done
}

echo '== armv7 lane =='
if [[ ! -f build-armv7/build.ninja ]]; then
    cmake -S . -B build-armv7 -G Ninja -DCINUX_ARCHITECTURE=armv7 || exit 1
fi
# The boot check opens the buildroot volume and boots the self-check
# image; neither rides on arm_image's dependency graph, so all three
# targets have to be asked for on a cold build directory. The desk joins
# the volume through the overlay stage.
cmake --build build-armv7 --target arm_image arm_selftest buildroot_volume || exit 1

fails=0
for ((run = 1; run <= repeat; ++run)); do
    if ctest --test-dir build-armv7 --output-on-failure; then
        printf 'run %s/%s: passed\n' "$run" "$repeat"
    else
        fails=$((fails + 1))
        printf 'run %s/%s: FAILED (serial log kept)\n' "$run" "$repeat"
    fi
done

echo '== x86 lane =='
if [[ -f build/build.ninja ]]; then
    cmake --build build --target test_host || exit 1
    cmake --build build --target kernel_bin || exit 1
else
    echo 'no build/ configured; skipping the host lane'
fi

show_size
if [[ "$repeat" -gt 1 ]]; then
    printf 'arm_boot: %s/%s passed\n' "$((repeat - fails))" "$repeat"
fi
if [[ "$fails" -ne 0 ]]; then
    printf 'kept logs:\n'
    ls -1t build-armv7/*.fail-*.log 2>/dev/null | head -3
    exit 1
fi
echo 'both lanes green'
