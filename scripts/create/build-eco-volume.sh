#!/usr/bin/env bash
# Bakes the ecosystem test volume: busybox plus the glibc runtime and the
# guest-side test binaries (hello-glibc, ecotest) layered on top of the
# self-host toolchain staging tree. the ecosystem and self-host checks
# (self-host) boot the kernel-only image with this volume on the spare AHCI
# seat via qemu-eco-args.txt; the run/run_gui development volume is never
# touched, and nothing here rides the card image.
set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <out.img> <build-dir>" >&2
    exit 2
fi

out="$1"
build="$2"
repo="$(cd "$(dirname "$0")/../.." && pwd)"

staging="${build}/toolchain-staging"
busybox="${build}/busybox-glibc/busybox"
hello="${build}/glibc/hello-glibc"
ecotest="${build}/glibc/ecotest"
desk="${build}/glibc/vesper-desk"
ttydemo="${build}/user/ttydemo.elf"
userhello="${build}/user/hello.elf"
smpstress="${build}/user/smpstress.elf"
for required in "${staging}/hello.s" "${busybox}" "${hello}" "${ecotest}" "${desk}" \
                "${ttydemo}" "${userhello}" "${smpstress}"; do
    if [ ! -e "$required" ]; then
        echo "[eco-volume] missing input: ${required}" >&2
        echo "[eco-volume] build busybox_glibc hello_glibc user_ecotest toolchain_staging vesper_desk user_ttydemo user_hello user_smpstress first" >&2
        exit 1
    fi
done

work="${build}/eco-rootfs"
rm -rf "$work"
mkdir -p "${work}"
cp -a "${staging}/." "${work}/"

# The shell seat: busybox with every applet hard-seated under /bin, and
# /sbin/init pointing back at it, matching the seeding the ecosystem
# checks type against.
install -Dm0755 "${busybox}" "${work}/bin/busybox"
while read -r applet; do
    [ "$applet" = "busybox" ] && continue
    ln -sf busybox "${work}/bin/${applet}"
done < <("${busybox}" --list)
mkdir -p "${work}/sbin"
ln -sf ../bin/busybox "${work}/sbin/init"
install -Dm0755 "${hello}" "${work}/bin/hello-glibc"
install -Dm0755 "${ecotest}" "${work}/bin/ecotest"
# The desktop lane rides this volume too: the desk binary plus the same
# session gate the Buildroot overlay uses, so the desktop check boots
# the same shape of guest the run_gui lane shows.
install -Dm0755 "${desk}" "${work}/bin/vesper"
# The freestanding probe programs the guest checks type by bare name;
# the build names carry .elf, the volume seats them without it.
install -Dm0755 "${ttydemo}" "${work}/bin/ttydemo"
install -Dm0755 "${userhello}" "${work}/bin/hello"
install -Dm0755 "${smpstress}" "${work}/bin/smpstress"
install -Dm0755 "${repo}/user/rootfs/buildroot/overlay/etc/init.d/desktop" \
    "${work}/etc/init.d/desktop"

# The glibc runtime the dynamic binaries bake into their PT_INTERP and
# rpath: the loader at /lib64, the shared objects at /lib.
loader="$(gcc -print-file-name=ld-linux-x86-64.so.2)"
libdir="$(dirname "$(readlink -f "$(gcc -print-file-name=libc.so.6)")")"
install -Dm0755 "${loader}" "${work}/lib64/ld-linux-x86-64.so.2"
for library in libc.so.6 libm.so.6 libresolv.so.2 libgcc_s.so.1; do
    candidate="${libdir}/${library}"
    [ -e "${candidate}" ] || candidate="$(gcc -print-file-name="${library}")"
    [ -e "${candidate}" ] || continue
    install -Dm0755 "${candidate}" "${work}/lib/${library}"
done

mkdir -p "${work}/etc" "${work}/root" "${work}/home/cinux" \
         "${work}/tmp" "${work}/proc" "${work}/sys" "${work}/dev"
printf '%s\n' \
    '::sysinit:/bin/echo Cinux up' \
    '::once:/bin/sh /etc/init.d/desktop' \
    'console::respawn:/bin/sh' > "${work}/etc/inittab"
printf '%s\n' 'none /tmp tmpfs rw 0 0' > "${work}/etc/fstab"
printf '%s\n' \
    'root:x:0:0:root:/root:/bin/sh' \
    'cinux:x:1000:1000:Cinux:/home/cinux:/bin/sh' > "${work}/etc/passwd"
printf '%s\n' 'root:x:0:' 'cinux:x:1000:' > "${work}/etc/group"
# The resolver the DNS probe reads: slirp's builtin server.
install -Dm0644 "${repo}/user/rootfs/buildroot/overlay/etc/resolv.conf" \
    "${work}/etc/resolv.conf"
install -Dm0755 "${repo}/scripts/create/ext2_seed/smp-stress.sh" \
    "${work}/etc/smp-stress.sh"

rm -f "${out}"
mkdir -p "$(dirname "${out}")"
mke2fs -q -t ext2 -b 1024 -O none -d "${work}" "${out}" 262144k
echo "[eco-volume] baked ${out}: busybox + glibc runtime + selfhost staging + test binaries"
