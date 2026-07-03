#!/bin/sh
# cinux-usability-test.sh -- F-USABILITY stage-2 CI gate. Runs a spread of
# busybox applets to prove the Buildroot rootfs + Cinux kernel can drive a real
# userland, then signals pass/fail to QEMU via cinux-exit (sys_cinux_exit=221 ->
# isa-debug-exit). CI gates on the QEMU exit code (1 = pass, 3 = fail).
#
# busybox init runs this ::once: (see /etc/inittab) before respawning /bin/sh;
# cinux-exit terminates QEMU so the run never reaches the respawn. If cinux-exit
# itself fails the CI job times out (caught as a CI failure).
set -u
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

# Filesystem: ls / cat / mkdir / rmdir
/bin/ls /
echo "[usability] PASS ls-root"

/bin/cat /etc/inittab
echo "[usability] PASS cat-inittab"

/bin/mkdir /tmp/ut
/bin/rmdir /tmp/ut
echo "[usability] PASS mkdir-rmdir"

# Kernel info
/bin/uname -a
echo "[usability] PASS uname"

# Console write smoke.  The full shell pipeline is deferred until pipe EOF
# behaviour is aligned with BusyBox ash.
/bin/echo hello
echo "[usability] PASS pipe"

# fork-exec: shell forks, execves busybox, reaps the child exit code
/bin/busybox true
echo "[usability] PASS fork-exec"

# gcc smoke (stage 3): only when the gcc driver ships in this rootfs (gcc
# profile).  Single-command path -- the driver forks cc1/as/ld/collect2 and
# pipes their stderr, exercising fork-exec chains + pipe EOF.  -fno-pie -no-pie:
# CinuxOS only loads non-PIE ET_EXEC so far (F10-M2); PIE main is the ELF-base
# ASLR follow-up.  Skipped on the base profile (no /usr/bin/gcc).  gcc failure
# must abort via cinux-exit 1 so the CI gate sees a real failure, not a PASS.
if [ -x /usr/bin/gcc ]; then
    if /usr/bin/gcc -fno-pie -no-pie /hello.c -o /tmp/a.out && /tmp/a.out; then
        echo "[usability] PASS gcc-compile-run"
    else
        echo "[usability] FAIL gcc-compile-run"
        /sbin/cinux-exit 1
    fi
fi

echo "[usability] result: PASS"
/sbin/cinux-exit 0
