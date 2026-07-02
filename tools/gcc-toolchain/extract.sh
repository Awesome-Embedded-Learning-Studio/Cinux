#!/bin/bash
# Stage the GCC toolchain subset for the B4-a as+ld smoke into a directory tree.
#
# Usage: extract.sh [output_dir]   (default build/gcc-root)
#
# NOT a self-built toolchain (user decision 2026-07-02: don't compile GCC). This
# copies the host's glibc-dynamic as/ld + their runtime .so closure + the crt
# objects + libgcc, laid out exactly as GCC's built-in specs expect (hardcoded
# /usr/lib, /usr/lib/gcc/<triple>/<ver>, /lib64 paths). cc1 and headers are
# deferred to B4-b -- as+ld do not read headers.
#
# The tree feeds scripts/create_ext2_disk.sh via its GCC_ROOT arg; mkfs.ext2 -d
# merges it (cp -a preserves SONAME symlinks like libbfd.so -> libbfd-2.46.0.so).

set -e

ROOT="${1:-build/gcc-root}"
GCC_INSTALL="$(dirname "$(gcc -print-file-name=crtbegin.o)")"  # /usr/lib/gcc/<triple>/<ver>

rm -rf "$ROOT"
mkdir -p "$ROOT/usr/bin" "$ROOT/usr/lib" "$ROOT/lib64" "$ROOT$GCC_INSTALL"

# --- binaries: as/ld only (cc1 + gcc driver land with B4-b) ---
cp -a /usr/bin/as "$ROOT/usr/bin/as"
cp -a /usr/bin/ld "$ROOT/usr/bin/ld"

# --- dynamic interpreter at the exact PT_INTERP path ---
# Resolve the real file (/lib64 is a symlink on Arch); install it as a regular
# file at /lib64/ld-linux-x86-64.so.2 -- the path every glibc-dynamic ELF asks for.
mkdir -p "$ROOT/lib64"
cp -aL /lib64/ld-linux-x86-64.so.2 "$ROOT/lib64/ld-linux-x86-64.so.2"

# Copy a .so, preserving its versioned name AND creating the SONAME symlink
# (libfoo.so.N -> libfoo.so.N.M.P) so ldso resolves DT_NEEDED.
cp_lib() {
    local src="$1"
    [ -f "$src" ] || return 0
    cp -aL "$src" "$ROOT/usr/lib/"
    local base soname
    base=$(basename "$src")
    soname=$(readelf -d "$src" 2>/dev/null | sed -n 's/.*SONAME.*\[\(.*\)\].*/\1/p' | head -1)
    if [ -n "$soname" ] && [ "$soname" != "$base" ]; then
        ln -sf "$base" "$ROOT/usr/lib/$soname"
    fi
}

# --- runtime .so closure of as/ld via ldd (libc/libm/libbfd/libctf/libjansson/
#     libz/libzstd/libsframe). ldd prints `lib => /path (addr)`; field 3 is the
#     path. ldso itself prints without `=>` and is skipped (handled above). ---
ldd /usr/bin/as /usr/bin/ld 2>/dev/null | grep '=> /' | awk '{print $3}' | sort -u | while read -r lib; do
    cp_lib "$lib"
done
# libgcc_s.so.1 is a GCC runtime ld may pull; ensure present.
cp_lib /usr/lib/libgcc_s.so.1

# --- link-time crt + libc + libgcc (ld needs these when linking hello.o -> hello) ---
for f in crt1.o Scrt1.o crti.o crtn.o; do
    cp -a "/usr/lib/$f" "$ROOT/usr/lib/" 2>/dev/null || true
done
for f in libc.so libc_nonshared.a libc.a; do
    cp -a "/usr/lib/$f" "$ROOT/usr/lib/" 2>/dev/null || true
done
for f in crtbegin.o crtbeginS.o crtbeginT.o crtend.o crtendS.o crtfastmath.o; do
    cp -a "$GCC_INSTALL/$f" "$ROOT$GCC_INSTALL/" 2>/dev/null || true
done
cp -aL "$(gcc -print-libgcc-file-name)" "$ROOT$GCC_INSTALL/"          # libgcc.a
cp -aL "$GCC_INSTALL/libgcc_eh.a" "$ROOT$GCC_INSTALL/" 2>/dev/null || true

# --- hello.s: host-precompiled as input for the B4-a as+ld smoke, plus the
#     hello.c source for B4-b cc1. ---
cat > /tmp/cinux_hello.c << 'EOF'
#include <stdio.h>
int main(void) {
    printf("Hello from GCC!\n");
    return 0;
}
EOF
# -fno-pie: CinuxOS only loads non-PIE ET_EXEC so far (F10-M2 hello-dyn), so emit
# a non-PIE assembly (absolute addressing, links with crt1.o + crtbegin.o under
# ld -no-pie). PIE main (Scrt1/crtbeginS, ET_DYN) is a follow-up with ELF-base ASLR.
gcc -S -fno-pie -o "$ROOT/hello.s" /tmp/cinux_hello.c
cp /tmp/cinux_hello.c "$ROOT/hello.c"
rm -f /tmp/cinux_hello.c

echo "[extract] GCC toolchain subset staged at $ROOT"
du -sh "$ROOT"
echo "[extract] binaries:"; ls "$ROOT/usr/bin"
echo "[extract] /usr/lib (.so + crt):"; ls "$ROOT/usr/lib"
echo "[extract] $GCC_INSTALL:"; ls "$ROOT$GCC_INSTALL"
