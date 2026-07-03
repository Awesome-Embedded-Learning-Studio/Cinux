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
# Arch GCC's default link specs pass -lgcc_s_asneeded and
# -latomic_asneeded through collect2/ld.  Stage both linker scripts and their
# targets so the single-command gcc path resolves exactly as it does on host.
for f in /usr/lib/libgcc_s.so /usr/lib/libgcc_s.so.1 \
         /usr/lib/libgcc_s_asneeded.so \
         /usr/lib/libatomic.so /usr/lib/libatomic.so.1 /usr/lib/libatomic.so.1.2.0 \
         /usr/lib/libatomic_asneeded.so; do
    cp_lib "$f"
done
cp -a /usr/lib/libatomic.a "$ROOT/usr/lib/" 2>/dev/null || true

# --- B4-C1: cc1 (GCC C front end, ~47 MB) + its .so closure.  cc1 pulls
#     libisl/libmpc/libmpfr/libgmp/libm on top of as/ld's libz/libzstd/libc.
#     `cc1 --version` needs NO headers, so this stages the binary + deps only;
#     /usr/include (the C headers, ~250 MB) lands with B4-C2 (actual compile). ---
cp -a "$GCC_INSTALL/cc1" "$ROOT$GCC_INSTALL/cc1"
ldd "$GCC_INSTALL/cc1" 2>/dev/null | grep '=> /' | awk '{print $3}' | sort -u | while read -r lib; do
    cp_lib "$lib"
done

# --- F-USABILITY stage 3: gcc driver + collect2 (single-command `gcc hello.c`
#     path).  The driver (~1.7 MB) forks cc1/as/ld and pipes their stderr; its
#     .so closure is just libc + ldso (already staged via as/ld, so the ldd
#     cp_lib below is a harmless no-op).  collect2 runs at link time to wire
#     .init_array constructors.  liblto_plugin.so is still needed because GCC's
#     default link path passes -fuse-linker-plugin even for this tiny C smoke.
#     cc1plus/lto1/lto-wrapper stay out: C-only smoke (g++ is a later batch). ---
cp -a /usr/bin/gcc "$ROOT/usr/bin/gcc"
cp -a "$GCC_INSTALL/collect2" "$ROOT$GCC_INSTALL/collect2"
cp -a "$(gcc -print-file-name=liblto_plugin.so)" "$ROOT$GCC_INSTALL/liblto_plugin.so"
ldd /usr/bin/gcc "$GCC_INSTALL/collect2" 2>/dev/null | grep '=> /' | awk '{print $3}' | sort -u | while read -r lib; do
    cp_lib "$lib"
done

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

# --- B4-C2: hello.c's #include closure (computed live via gcc -H) so cc1 can
#     actually compile hello.c on Cinux.  The closure is tiny (~25 files,
#     ~200 KB); stage exactly those at their absolute paths instead of all
#     ~249 MB of /usr/include.  cc1's built-in include search (/usr/include +
#     $GCC_INSTALL/include) then resolves <stdio.h> et al.  [ -f ] filters the
#     "Multiple include guards ..." non-path line gcc -H appends. ---
gcc -H -fsyntax-only -fno-pie "$ROOT/hello.c" 2>&1 >/dev/null \
    | sed -E 's/^[. ]+//' | grep -v '^$' | sort -u | while read -r h; do
    [ -f "$h" ] || continue
    install -Dm0644 "$h" "$ROOT$h"
done
# GCC's cc1 implicitly pre-includes <stdc-predef.h> before the C source (and
# glibc features.h:518 includes it explicitly).  gcc -H does NOT list this
# implicit pre-include, so the closure above misses it; without it cc1 fails
# at features.h:518 "fatal error: stdc-predef.h: No such file or directory".
[ -f /usr/include/stdc-predef.h ] && install -Dm0644 /usr/include/stdc-predef.h "$ROOT/usr/include/stdc-predef.h"

echo "[extract] GCC toolchain subset staged at $ROOT"
du -sh "$ROOT"
echo "[extract] binaries:"; ls "$ROOT/usr/bin"
echo "[extract] /usr/lib (.so + crt):"; ls "$ROOT/usr/lib"
echo "[extract] $GCC_INSTALL:"; ls "$ROOT$GCC_INSTALL"
