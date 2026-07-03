# =============================================================================
# cmake/options.cmake
# @brief Single registry of every user-facing Cinux build switch.
#
# Add a new switch HERE, not scattered across CMakeLists.txt files. Switches in
# CINUX_COMPILE_DEF_OPTS below are auto-mapped to a PUBLIC compile definition of
# the same name on big_kernel_common (kernel/CMakeLists.txt foreach) -- so a new
# driver/smoke flag is one option() here + one list entry, nothing else.
#
# Groups:
#   1. Feature drivers      -- hardware subsystems (GUI/USB/NET)
#   2. Debug instrumentation -- opt-in diagnostics (LOCKDEP/UBSAN)
#   3. Ring-3 smoke tests   -- musl/busybox/gcc ecosystem probes
#   4. Host sanitizers      -- test/ only (HOST_ASAN/HOST_TSAN)
#   5. Build switches       -- what gets built (BUILD_TESTS)
# =============================================================================

# ---- 1. Feature drivers -----------------------------------------------------
option(CINUX_GUI "Enable GUI mode (window manager + terminal + host adapter)" ON)
option(CINUX_USB "Enable USB (xHCI host controller + PCI MSI-X) driver" ON)
option(CINUX_NET "Enable e1000 Intel NIC driver + L3/L4 stack" ON)

# ---- 2. Debug instrumentation (opt-in, zero cost when off) ------------------
# F-INFRA I-10: assert no spinlock is held across schedule() (would deadlock
# single-core). Enable for deadlock hunting: cmake -DCINUX_LOCKDEP=ON ...
option(CINUX_LOCKDEP "Enable lockdep schedule-while-locked debug checks" OFF)

# F-INFRA I-9: -fsanitize=undefined instruments the kernel to call freestanding
# __ubsan_handle_* stubs (kernel/lib/ubsan.cpp) that kpanic on UB. Compile-only
# (no libubsan at link -- the stubs resolve the references). Enable for UB
# hunting: cmake -DCINUX_UBSAN=ON ... NOTE: handled separately from the foreach
# below because it also needs set_source_files_properties to exclude the stub +
# diagnostic path (see kernel/CMakeLists.txt).
option(CINUX_UBSAN "Enable -fsanitize=undefined with freestanding UBSan stubs" OFF)

# ---- 3. Ring-3 smoke tests (run-kernel-test harness; need artifacts) --------
# F10-M1 batch 6 / P3: musl /hello ring-3 smoke -- the ONLY test that exercises
# real user-space syscall paths under SMAP (run-kernel-test uses kernel
# addresses so SMAP never fires there). Requires musl sysroot + /hello +
# /forktest on ext2 (tools/musl/). CI caches the sysroot; ~30s one-time locally.
option(CINUX_MUSL_HELLO_SMOKE "Enable musl /hello ring-3 smoke in run-kernel-test" ON)

# F10-M2: musl DYNAMIC /hello-dyn ring-3 smoke (PT_INTERP / interpreter-load
# path; ldso relocates the program in user space). Requires musl sysroot +
# /hello-dyn + /lib/ld-musl-x86_64.so.1. Default OFF (CI has no sysroot).
option(CINUX_MUSL_DYN_SMOKE "Enable musl dynamic /hello-dyn ring-3 smoke in run-kernel-test" OFF)

# F-ECO: busybox ring-3 ecosystem smoke. fork+execve /bin/busybox to run a
# spread of applets -- the first "run real programs" touchstone. Requires the
# busybox ELF at build/musl/busybox (tools/musl/build-busybox.sh; not a CMake
# target). Default OFF; CI enables it in the dedicated busybox-smoke job.
option(CINUX_BUSYBOX_SMOKE "Enable busybox ring-3 ecosystem smoke in run-kernel-test" OFF)

# B4-B1: optionally stage the host's glibc-dynamic GCC toolchain subset
# (as/ld + glibc runtime + crt + libgcc; no cc1/headers) into the ext2 disk for
# the GCC self-host smoke. Off by default: CI lacks GCC-private crt, and the
# subset is a host artifact. cmake/qemu.cmake acts on this flag (disk size +
# staging); kernel/CMakeLists.txt maps it to a compile definition.
option(CINUX_GCC_TOOLCHAIN "Stage host glibc GCC subset (as/ld+crt+libgcc) into ext2 disk" OFF)

# ---- 4. Host unit test sanitizers (test/ only; zero kernel change) ----------
# F-QA Q1-5: opt-in ASan + UBSan + gcov for host unit tests. Catches UAF/OOB/UB
# at the InodeOps/mock layer. TSan is mutually exclusive (separate flag below).
option(CINUX_HOST_ASAN "Host unit tests: AddressSanitizer + UBSan + gcov coverage" OFF)

# F-VERIFY M4: opt-in ThreadSanitizer -- the data-race detector (lockdep only
# does lock-order). Mutually exclusive with CINUX_HOST_ASAN. Applies to the
# -pthread tests (pmm concurrent, sync_concurrent).
option(CINUX_HOST_TSAN "Host unit tests: ThreadSanitizer (data-race detector for -pthread tests)" OFF)

# ---- 5. Build switches ------------------------------------------------------
# Controls whether test/ (host unit tests + test kernel image) is added. OFF by
# default for zero behaviour change: historically injected via -D from CI
# (.github/workflows/ci.yml), VSCode (.vscode/tasks.json), and scripts; never
# declared, so a bare `cmake -B build -S .` silently skipped test/. Declaring
# it makes it visible in ccmake/cmake-gui without changing the default.
option(CINUX_BUILD_TESTS "Build host unit tests (test/) + test kernel image" OFF)

# =============================================================================
# Rootfs profile (F-USABILITY stage 2)
# =============================================================================
# Selects the rootfs the `run` / `run-*` QEMU targets attach as the ext2 disk.
#   handcrafted (default): create_ext2_disk.sh-built ext2.img (the kernel test
#     rootfs with /hello, /forktest, busybox, ...).
#   buildroot: an external Buildroot rootfs.ext2 pointed at by
#     CINUX_ROOTFS_BUILDROOT_IMG (real Linux userland; the buildroot-usability
#     CI gate). Back-compat: setting CINUX_ROOTFS_BUILDROOT_IMG on first
#     configure without an explicit profile defaults the profile to "buildroot"
#     (the F-USABILITY stage-1 workflow sets just the img path).
set(CINUX_ROOTFS_BUILDROOT_IMG ""
    CACHE FILEPATH "Buildroot rootfs.ext2 (used when CINUX_ROOTFS_PROFILE=buildroot); empty otherwise")
if(NOT DEFINED CINUX_ROOTFS_PROFILE)
    if(DEFINED CINUX_ROOTFS_BUILDROOT_IMG AND CINUX_ROOTFS_BUILDROOT_IMG)
        set(CINUX_ROOTFS_PROFILE "buildroot")
    else()
        set(CINUX_ROOTFS_PROFILE "handcrafted")
    endif()
endif()
set(CINUX_ROOTFS_PROFILE "${CINUX_ROOTFS_PROFILE}"
    CACHE STRING "rootfs profile: handcrafted | buildroot")
set_property(CACHE CINUX_ROOTFS_PROFILE PROPERTY STRINGS handcrafted buildroot)

# =============================================================================
# Switches that map 1:1 to a same-named PUBLIC compile definition on
# big_kernel_common. kernel/CMakeLists.txt loops over this list instead of one
# if() per flag. CINUX_UBSAN is intentionally absent (see comment above).
# =============================================================================
set(CINUX_COMPILE_DEF_OPTS
    GUI USB NET LOCKDEP
    MUSL_HELLO_SMOKE MUSL_DYN_SMOKE BUSYBOX_SMOKE GCC_TOOLCHAIN)
