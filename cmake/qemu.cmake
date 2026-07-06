find_program(QEMU_EXECUTABLE qemu-system-x86_64)

if(NOT QEMU_EXECUTABLE)
    set(QEMU_EXECUTABLE "qemu-system-x86_64")
    message(WARNING "qemu-system-x86_64 not found in PATH, using default name")
endif()

# Detect KVM — skip -accel kvm when /dev/kvm is absent (e.g. CI runners)
# or when CINUX_NO_KVM is set (force TCG / 2MB-path for diagnosis).
if(EXISTS "/dev/kvm" AND NOT DEFINED ENV{CINUX_NO_KVM})
    set(QEMU_ACCEL -accel kvm -cpu max)
else()
    # No KVM (CI runners, or CINUX_NO_KVM force-TCG). Use -cpu max so SMAP/SMEP
    # are emulated; the default qemu64 advertises neither, so F9 batch 4's
    # stac/clac instructions #UD (CR4.SMAP can't be set without CPUID support).
    set(QEMU_ACCEL -cpu max)
endif()

# Headless mode for CI (no GTK/display available)
if(DEFINED ENV{CI})
    set(QEMU_MEMORY "1G")
    set(QEMU_DISPLAY -vnc :0)
else()
    set(QEMU_MEMORY "8G")
    set(QEMU_DISPLAY -vnc :0)
endif()

set(QEMU_COMMON_FLAGS
    -m ${QEMU_MEMORY}
    -serial stdio
    -no-reboot
    -debugcon file:debug.log
    -global isa-debugcon.iobase=0xe9
    ${QEMU_ACCEL}
    ${QEMU_DISPLAY}
    -usb
)

set(QEMU_DEVELOP_FLAG     
    -no-shutdown)

# AHCI test disk (1 MB, with MBR boot signature at offset 510-511)
set(AHCI_TEST_IMAGE "${CMAKE_BINARY_DIR}/ahci_test.img")
add_custom_command(
    OUTPUT ${AHCI_TEST_IMAGE}
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/create_ahci_test_disk.sh ${AHCI_TEST_IMAGE}
    COMMENT "Creating AHCI test disk image"
    VERBATIM
)

# F5-M3 NVMe: test disk (1 MB raw).  The batch-1 mechanism test reads CAP/VS via
# MMIO, so disk content is irrelevant -- the file just backs -device nvme so the
# controller enumerates and its BAR0 maps.
set(NVME_TEST_IMAGE "${CMAKE_BINARY_DIR}/nvme_test.img")
add_custom_command(
    OUTPUT ${NVME_TEST_IMAGE}
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/create_ahci_test_disk.sh ${NVME_TEST_IMAGE}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/create_ahci_test_disk.sh
    COMMENT "Creating NVMe test disk image"
    VERBATIM
)

# ext2 filesystem disk image (4 MB, mounted at AHCI port 1)
set(EXT2_IMAGE "${CMAKE_BINARY_DIR}/ext2.img")

# F-USABILITY: rootfs image selected by CINUX_ROOTFS_PROFILE (set in
# cmake/options.cmake; CINUX_ROOTFS_BUILDROOT_IMG supplies the buildroot path).
#   buildroot   -- external Buildroot rootfs.ext2 (real Linux userland; the
#                  buildroot-usability CI gate). No CMake rebuild dep.
#   handcrafted -- create_ext2_disk.sh-built ext2.img (default; the kernel
#                  test rootfs with /hello, /forktest, busybox, ...).
if(CINUX_ROOTFS_PROFILE STREQUAL "buildroot")
    if(NOT CINUX_ROOTFS_BUILDROOT_IMG)
        message(FATAL_ERROR
            "CINUX_ROOTFS_PROFILE=buildroot requires -DCINUX_ROOTFS_BUILDROOT_IMG=<rootfs.ext2>")
    endif()
    set(ROOTFS_IMG "${CINUX_ROOTFS_BUILDROOT_IMG}")
    set(ROOTFS_DEPS "")                  # 外部产,CMake 不重建
else()
    set(ROOTFS_IMG "${EXT2_IMAGE}")
    set(ROOTFS_DEPS "${EXT2_IMAGE}")     # create_ext2_disk.sh custom command OUTPUT
endif()
set(USER_SHELL_ELF "${CMAKE_BINARY_DIR}/user/shell")
# F10-M1 batch 6: musl static hello at /hello when present (built by
# tools/musl/build-musl.sh + build-hello.sh; not a CMake target, so not a hard
# dependency — the script includes it iff the file exists, and the ring-3 smoke
# test skips when /hello is absent).
set(MUSL_HELLO_ELF "${CMAKE_BINARY_DIR}/musl/hello")
# F-VERIFY M5-2: musl static SMP CoW-race reproducer at /forktest when present
# (built by tools/musl/build-forktest.sh; same conditional-include pattern as
# /hello).  The ring-3 smoke execve's it under -smp 2 to gate the F10 CoW fixes.
set(MUSL_FORKTEST_ELF "${CMAKE_BINARY_DIR}/musl/forktest")
# F10-M2: musl DYNAMIC hello at /hello-dyn + its interpreter at the PT_INTERP
# path /lib/ld-musl-x86_64.so.1, when present (built by tools/musl/build-musl.sh
# + build-hello-dyn.sh; not CMake targets). Same conditional-include pattern as
# /hello: the script includes them iff the files exist (absent in CI).
set(MUSL_HELLO_DYN_ELF "${CMAKE_BINARY_DIR}/musl/hello-dyn")
set(MUSL_LDSO_ELF "${CMAKE_BINARY_DIR}/musl-sysroot/lib/libc.so")
# F-ECO batch 0: minimal static busybox at /bin/busybox when present (built by
# clang --target=x86_64-linux-musl, not a CMake target). The ring-3 smoke
# fork+execves it to run echo/cat/ls applets -- the first ecosystem touchstone.
set(BUSYBOX_ELF "${CMAKE_BINARY_DIR}/musl/busybox")

# B4-B1: optionally stage the host's glibc-dynamic GCC toolchain subset
# (as/ld + glibc runtime + crt + libgcc; no cc1/headers yet) into the ext2 disk
# for the GCC self-host smoke. Off by default: CI lacks GCC-private crt, and the
# subset is a host artifact, not a CinuxOS build product. Local builds enable it
# with -DCINUX_GCC_TOOLCHAIN=ON. (The option() itself lives in cmake/options.cmake.)
if(CINUX_GCC_TOOLCHAIN)
    set(GCC_ROOT "${CMAKE_BINARY_DIR}/gcc-root")
    set(GCC_ROOT_DEP "${CMAKE_BINARY_DIR}/gcc-root.stamp")
    set(EXT2_DISK_SIZE 128)
    set(EXT2_DISK_INODES 8192)
    add_custom_command(
        OUTPUT ${GCC_ROOT_DEP}
        COMMAND ${CMAKE_SOURCE_DIR}/tools/gcc-toolchain/extract.sh ${GCC_ROOT}
        COMMAND ${CMAKE_COMMAND} -E touch ${GCC_ROOT_DEP}
        DEPENDS ${CMAKE_SOURCE_DIR}/tools/gcc-toolchain/extract.sh
        COMMENT "Staging GCC toolchain subset (as/ld + glibc runtime + crt)"
        VERBATIM
    )
else()
    set(GCC_ROOT "")
    set(GCC_ROOT_DEP "")
    set(EXT2_DISK_SIZE 8)
    set(EXT2_DISK_INODES 1024)
endif()

add_custom_command(
    OUTPUT ${EXT2_IMAGE}
    COMMAND ${CMAKE_COMMAND} -E env IMAGE_SIZE=${EXT2_DISK_SIZE} INODES=${EXT2_DISK_INODES}
            ${CMAKE_SOURCE_DIR}/scripts/create_ext2_disk.sh ${EXT2_IMAGE} ${USER_SHELL_ELF}
            ${MUSL_HELLO_ELF} ${MUSL_FORKTEST_ELF} ${MUSL_HELLO_DYN_ELF} ${MUSL_LDSO_ELF}
            ${BUSYBOX_ELF} ${GCC_ROOT}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/create_ext2_disk.sh user_shell ${GCC_ROOT_DEP}
    COMMENT "Creating ext2 image with /bin/sh (+ GCC toolchain if CINUX_GCC_TOOLCHAIN)"
    VERBATIM
)

# ext4 (extents) filesystem disk image (8 MB, mounted at AHCI port 2).
# F6-M5: dedicated extent-mapped volume for the ext4 read-path mechanism test.
# Forced to 32-byte group descriptors (^64bit,^metadata_csum) so the ext2
# driver's fixed-stride BGDT read resolves bg_inode_table correctly.
set(EXT4_IMAGE "${CMAKE_BINARY_DIR}/ext4.img")
add_custom_command(
    OUTPUT ${EXT4_IMAGE}
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/create_ext4_disk.sh ${EXT4_IMAGE}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/create_ext4_disk.sh
    COMMENT "Creating ext4 (extents) filesystem image with /big.bin + /small.txt"
    VERBATIM
)

# QEMU 额外测试标志（添加到 COMMON_FLAGS 之上）
set(QEMU_TEST_EXTRA_FLAGS
    -device isa-debug-exit,iobase=0xf4,iosize=0x04
    -device ahci,id=ahci
    -drive file=${AHCI_TEST_IMAGE},format=raw,if=none,id=ahci-disk
    -device ide-hd,drive=ahci-disk,bus=ahci.0
    -drive file=${EXT2_IMAGE},format=raw,if=none,id=ext2-disk
    -device ide-hd,drive=ext2-disk,bus=ahci.1
    -drive file=${EXT4_IMAGE},format=raw,if=none,id=ext4-disk
    -device ide-hd,drive=ext4-disk,bus=ahci.2
    # F5-M3 NVMe: controller + 1 MB backing disk.  serial= is mandatory for
    # -device nvme; the kernel enumerates via PCI class 0x01/0x08.
    -drive file=${NVME_TEST_IMAGE},format=raw,if=none,id=nvme-disk
    -device nvme,id=nvme0,serial=nvme0
    -device nvme-ns,drive=nvme-disk,nsid=1,bus=nvme0
)

# ============================================================
# isa-debug-exit Exit Code Mapping
# ============================================================
# QEMU's isa-debug-exit device encodes: exit_code = (value << 1) | 1
#   Kernel writes 0 → QEMU exits 1   → test SUCCESS
#   Kernel writes 1 → QEMU exits 3   → test FAILURE (unit test failed)
#   Panic writes a cause-coded value → QEMU exits (value<<1)|1, FAST (no
#   cli;hlt → timeout stall): exception panic value = vector+2 (#DF(8)→21,
#   #PF(14)→33, #GP(13)→31), generic kpanic value = 64 → exit 129.
# The run-kernel-test and run-stress-test targets use qemu_test_wrapper.sh,
# which maps exit 1 → success, 3 → failure, and labels panic exits with their
# decoded vector. Decode: vector = (exit_code - 1)/2 - 2  (129 = generic kpanic).

# 将 CMake list 转换为空格分隔的字符串（用于脚本生成）
string(REPLACE ";" " " QEMU_COMMON_FLAGS_STR "${QEMU_COMMON_FLAGS}")
string(REPLACE ";" " " QEMU_TEST_EXTRA_FLAGS_STR "${QEMU_TEST_EXTRA_FLAGS}")
set(QEMU_COMMON_FLAGS_STR "${QEMU_COMMON_FLAGS_STR}" CACHE INTERNAL "")
set(QEMU_TEST_EXTRA_FLAGS_STR "${QEMU_TEST_EXTRA_FLAGS_STR}" CACHE INTERNAL "")

# Set the debug console as 0xe9
# -s: GDB stub on :1234
# -S: Stop at startup (for debugging)
set(QEMU_DEBUG_FLAGS
    -s
    -S
)

if(NOT CINUX_IMAGE_PATH)
    message(STATUS "Image Path not specified yet, using default")
    set(CINUX_IMAGE_PATH "${CMAKE_BINARY_DIR}/cinux.img" CACHE PATH "Cinux disk image path")
endif()

# Let We make boots before sessions
set(MBR_BIN    "${CMAKE_BINARY_DIR}/boot/mbr.bin")
set(STAGE2_BIN "${CMAKE_BINARY_DIR}/boot/stage2.bin")
set(MINI_BIN   "${CMAKE_BINARY_DIR}/kernel/mini/mini_kernel.bin")
set(BIG_KERNEL_BIN "${CMAKE_BINARY_DIR}/kernel/big/big_kernel")
add_custom_command(
    OUTPUT ${CINUX_IMAGE_PATH}
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/build_image.sh
        ${MBR_BIN}
        ${STAGE2_BIN}
        ${MINI_BIN}
        ${CINUX_IMAGE_PATH}
        ${BIG_KERNEL_BIN}
    DEPENDS mbr stage2 mini_kernel big_kernel
    COMMENT "Building disk image: ${CINUX_IMAGE_PATH}"
    VERBATIM
)

add_custom_target(image ALL
    DEPENDS ${CINUX_IMAGE_PATH}
)

# ============================================================
# QEMU run-target factories
# ============================================================
# The run-* targets below share almost all of their QEMU command line; these
# helpers capture the variation along orthogonal axes (SMP / device set / debug
# / wrapper-vs-direct / image). QEMU argument order is irrelevant here --
# qemu_test_wrapper.sh just does "$@" and maps the exit code, and the configured
# run_all_tests.sh.in consumes pre-expanded strings -- so the factory may freely
# reorder. The hand-written targets stay: run-kernel-test-all (two legs),
# run-big-kernel-test / run-stress-test (distinct image + deps), run-kernel-
# test-debug / -interactive (direct QEMU on the test image), run-gdb.
function(cinux_qemu_test_target name)
    set(opts SMP DEV_NET DEV_XHCI)
    cmake_parse_arguments(ARG "${opts}" "COMMENT" "" ${ARGN})
    set(_smp)
    if(ARG_SMP)
        set(_smp -smp 2)
    endif()
    set(_devs)
    if(ARG_DEV_NET)
        list(APPEND _devs -device e1000,netdev=net0 -netdev user,id=net0)
    endif()
    if(ARG_DEV_XHCI)
        list(APPEND _devs -device qemu-xhci,id=xhci
                          -device usb-kbd,bus=xhci.0
                          -device usb-tablet,bus=xhci.0)
    endif()
    add_custom_target(${name}
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/qemu_test_wrapper.sh
                ${QEMU_EXECUTABLE} ${QEMU_COMMON_FLAGS} ${_smp} ${QEMU_TEST_EXTRA_FLAGS}
                ${_devs}
                -drive file=${CINUX_TEST_IMAGE_PATH},format=raw,index=0,media=disk
        DEPENDS check_uaccess_boundaries test-image ${AHCI_TEST_IMAGE}
                regenerate-ext2-image ${EXT4_IMAGE} ${NVME_TEST_IMAGE}
        USES_TERMINAL
        COMMENT "${ARG_COMMENT}"
        VERBATIM)
endfunction()

function(cinux_qemu_run_target name)
    set(opts SMP DEV_NET DEV_XHCI DEBUG)
    cmake_parse_arguments(ARG "${opts}" "COMMENT" "" ${ARGN})
    set(_smp)
    if(ARG_SMP)
        set(_smp -smp 2)
    endif()
    set(_dbg)
    if(ARG_DEBUG)
        set(_dbg ${QEMU_DEBUG_FLAGS})
    endif()
    set(_devs)
    set(_deps image)
    if(ARG_DEV_NET)
        list(APPEND _devs -device e1000,netdev=net0 -netdev user,id=net0)
    endif()
    if(ARG_DEV_XHCI)
        list(APPEND _devs -device qemu-xhci,id=xhci
                          -device usb-kbd,bus=xhci.0
                          -device usb-tablet,bus=xhci.0)
    endif()
    if(NOT ARG_DEBUG)
        # Interactive run targets attach the AHCI + rootfs disk group; run-debug
        # is a bare image + gdb boot (no extra disks, no extra deps).
        list(APPEND _devs -device ahci,id=ahci
                -drive file=${AHCI_TEST_IMAGE},format=raw,if=none,id=ahci-disk
                -device ide-hd,drive=ahci-disk,bus=ahci.0
                -drive file=${ROOTFS_IMG},format=raw,if=none,id=ext2-disk
                -device ide-hd,drive=ext2-disk,bus=ahci.1
                # F5-M3 NVMe: independent second disk (并存; production rootfs stays
                # on AHCI, NVMe is the perf-comparison / future-NVMe-Ext2 target).
                -drive file=${NVME_TEST_IMAGE},format=raw,if=none,id=nvme-disk
                -device nvme,id=nvme0,serial=nvme0
                -device nvme-ns,drive=nvme-disk,nsid=1,bus=nvme0)
        list(APPEND _deps ${AHCI_TEST_IMAGE} ${ROOTFS_DEPS} ${NVME_TEST_IMAGE})
    endif()
    add_custom_target(${name}
        COMMAND ${QEMU_EXECUTABLE} ${QEMU_COMMON_FLAGS} ${_smp} ${QEMU_DEVELOP_FLAG} ${_dbg}
                ${_devs}
                -drive file=${CINUX_IMAGE_PATH},format=raw,index=0,media=disk
        DEPENDS ${_deps}
        USES_TERMINAL
        COMMENT "${ARG_COMMENT}"
        VERBATIM)
endfunction()

cinux_qemu_run_target(run SMP DEV_NET DEV_XHCI COMMENT "Starting QEMU (serial: stdio)")

# Single-CPU run: same devices as `run` but WITHOUT -smp 2. The shell-launch
# fork #DF saga is -smp-2-only; single-CPU is stable, so this is the path to
# launch external programs from the shell without hitting the saga.
cinux_qemu_run_target(run-single DEV_NET DEV_XHCI
    COMMENT "Starting QEMU single-CPU (shell fork stable; no -smp 2 saga)")

# F4-M3 P2-4: SMP smoke -- same as `run` but with 2 CPUs, to exercise AP boot.
cinux_qemu_run_target(run-smp SMP COMMENT "Starting QEMU with 2 CPUs (SMP)")

cinux_qemu_run_target(run-debug DEBUG COMMENT "Starting QEMU in debug mode (GDB on :1234)")


add_custom_target(run-gdb
    COMMAND gdb -x ${CMAKE_SOURCE_DIR}/scripts/.gdbinit
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS run-debug
    COMMENT "GDB: connect to QEMU :1234 (64-bit big_kernel symbols via scripts/.gdbinit)"
    VERBATIM)

# ==============================================================
# Test Kernel Targets
# ==============================================================

set(MINI_TEST_BIN "${CMAKE_BINARY_DIR}/kernel/mini/mini_kernel_test.bin")
set(BIG_KERNEL_TEST_ELF "${CMAKE_BINARY_DIR}/kernel/big/big_kernel_test_crc.bin")

# 测试内核磁盘镜像
set(CINUX_TEST_IMAGE_PATH "${CMAKE_BINARY_DIR}/cinux_test.img")

add_custom_command(
    OUTPUT ${CINUX_TEST_IMAGE_PATH}
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/build_image.sh
        ${MBR_BIN}
        ${STAGE2_BIN}
        ${MINI_TEST_BIN}
        ${CINUX_TEST_IMAGE_PATH}
        ${BIG_KERNEL_TEST_ELF}
    DEPENDS mbr stage2 mini_kernel_test big_kernel_test
    COMMENT "Building test disk image: ${CINUX_TEST_IMAGE_PATH}"
    VERBATIM
)

add_custom_target(test-image
    DEPENDS ${CINUX_TEST_IMAGE_PATH}
)

# ==============================================================
# Stress Test Targets (1GB kernel load)
# ==============================================================

set(STRESS_KERNEL_ELF "${CMAKE_BINARY_DIR}/stress_kernel.elf")

add_custom_command(
    OUTPUT ${STRESS_KERNEL_ELF}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/generate_large_elf.py
        --size 1073741824
        --output ${STRESS_KERNEL_ELF}
    COMMENT "Generating 1GB stress test ELF"
    VERBATIM
)

add_custom_target(stress-kernel-elf
    DEPENDS ${STRESS_KERNEL_ELF}
)

# Stress test disk image: mini test kernel + 1GB synthetic ELF
set(STRESS_TEST_IMAGE "${CMAKE_BINARY_DIR}/cinux_stress_test.img")

add_custom_command(
    OUTPUT ${STRESS_TEST_IMAGE}
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/build_image.sh
        ${MBR_BIN}
        ${STAGE2_BIN}
        ${MINI_TEST_BIN}
        ${STRESS_TEST_IMAGE}
        ${STRESS_KERNEL_ELF}
    DEPENDS mbr stage2 mini_kernel_test stress-kernel-elf
    COMMENT "Building stress test disk image (1GB kernel)"
    VERBATIM
)

add_custom_target(stress-test-image
    DEPENDS ${STRESS_TEST_IMAGE}
)

# ==============================================================
# Big Kernel Test Target (production mini kernel + test big kernel)
# ==============================================================
# Uses the production mini kernel (which loads and jumps to big kernel)
# with the big_kernel_test binary (which has a test main instead of production main).

set(BIG_KERNEL_QEMU_TEST_ELF "${CMAKE_BINARY_DIR}/kernel/big/big_kernel_test")
set(BIG_KERNEL_QEMU_TEST_IMAGE "${CMAKE_BINARY_DIR}/cinux_big_test.img")

add_custom_command(
    OUTPUT ${BIG_KERNEL_QEMU_TEST_IMAGE}
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/build_image.sh
        ${MBR_BIN} ${STAGE2_BIN} ${MINI_BIN}
        ${BIG_KERNEL_QEMU_TEST_IMAGE}
        ${BIG_KERNEL_QEMU_TEST_ELF}
    DEPENDS mbr stage2 mini_kernel big_kernel_test
    COMMENT "Building big kernel test disk image"
    VERBATIM
)

add_custom_target(big-kernel-test-image
    DEPENDS ${BIG_KERNEL_QEMU_TEST_IMAGE}
)

add_custom_target(run-big-kernel-test
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qemu_test_wrapper.sh
        ${QEMU_EXECUTABLE} ${QEMU_COMMON_FLAGS} ${QEMU_TEST_EXTRA_FLAGS}
        -drive file=${BIG_KERNEL_QEMU_TEST_IMAGE},format=raw,index=0,media=disk
    DEPENDS big-kernel-test-image
    USES_TERMINAL
    COMMENT "Running big kernel GDT/IDT tests in QEMU"
    VERBATIM
)

add_custom_target(run-stress-test
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qemu_test_wrapper.sh
        ${QEMU_EXECUTABLE} ${QEMU_COMMON_FLAGS} ${QEMU_TEST_EXTRA_FLAGS}
        -drive file=${STRESS_TEST_IMAGE},format=raw,index=0,media=disk,cache=unsafe
    DEPENDS stress-test-image
    USES_TERMINAL
    COMMENT "Running 1GB kernel stress test"
    VERBATIM
)

# 运行测试内核（自动退出模式）
# 每次 run-kernel-test 前强制重建 ext2.img，确保磁盘状态干净
add_custom_target(regenerate-ext2-image
    COMMAND ${CMAKE_COMMAND} -E remove -f ${EXT2_IMAGE}
    COMMAND ${CMAKE_COMMAND} -E env IMAGE_SIZE=${EXT2_DISK_SIZE} INODES=${EXT2_DISK_INODES}
            ${CMAKE_SOURCE_DIR}/scripts/create_ext2_disk.sh ${EXT2_IMAGE} ${USER_SHELL_ELF}
            ${MUSL_HELLO_ELF} ${MUSL_FORKTEST_ELF} ${MUSL_HELLO_DYN_ELF} ${MUSL_LDSO_ELF}
            ${BUSYBOX_ELF} ${GCC_ROOT}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/create_ext2_disk.sh user_shell ${GCC_ROOT_DEP}
    COMMENT "Regenerating ext2 disk image for clean test state"
    VERBATIM
)

add_custom_target(check_uaccess_boundaries
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_uaccess_boundaries.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking user/kernel access boundary invariants"
    VERBATIM
)

cinux_qemu_test_target(run-kernel-test DEV_NET
    COMMENT "Starting QEMU with TEST kernel (auto-exit)")

# F5-M6 e1000: dedicated NIC-bringup smoke (same suite, explicit -device e1000).
cinux_qemu_test_target(run-kernel-test-net DEV_NET
    COMMENT "Starting QEMU with TEST kernel + e1000 NIC (auto-exit)")

# F5-M5: run the test kernel with a qemu-xHCI controller + usb-kbd/usb-tablet
# attached, so the xHCI tests (find_xhci + reset + enumeration + HID) have a
# real controller to exercise. The pointing device is a usb-tablet (absolute)
# so the cursor tracks the host cursor (a relative usb-mouse drifts at the edge).
cinux_qemu_test_target(run-kernel-test-xhci DEV_XHCI
    COMMENT "Starting QEMU with TEST kernel + qemu-xhci (auto-exit)")

# F4-M3 P2-4: SMP test kernel -- same suite but with 2 CPUs (auto-exit).
cinux_qemu_test_target(run-kernel-test-smp SMP
    COMMENT "Starting QEMU with TEST kernel + 2 CPUs (auto-exit)")

# F-VERIFY: 统一入口 -- 一条命令顺序跑 单核 → -smp 2 两套内核测试。
# 目的:AI/CI 验证时"一个指令全跑",消除"忘跑 -smp 变体"的流程盲区(47/47
# SMP 空转就是没人跑 -smp 的流程漏洞,不只是代码漏洞)。两条 COMMAND 顺序执行
# (不用 DEPENDS,免得 -j 并发两个 QEMU 抢同一 ext2/serial)。run-kernel-test /
# run-kernel-test-smp 保留为单独入口供聚焦调试。改单/双核 flag 时三处同步。
add_custom_target(run-kernel-test-all
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qemu_test_wrapper.sh
        ${QEMU_EXECUTABLE} ${QEMU_COMMON_FLAGS} ${QEMU_TEST_EXTRA_FLAGS}
        -device e1000,netdev=net0 -netdev user,id=net0
        -drive file=${CINUX_TEST_IMAGE_PATH},format=raw,index=0,media=disk
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qemu_test_wrapper.sh
        ${QEMU_EXECUTABLE} ${QEMU_COMMON_FLAGS} -smp 2 ${QEMU_TEST_EXTRA_FLAGS}
        -drive file=${CINUX_TEST_IMAGE_PATH},format=raw,index=0,media=disk
    DEPENDS check_uaccess_boundaries test-image ${AHCI_TEST_IMAGE} regenerate-ext2-image ${EXT4_IMAGE} ${NVME_TEST_IMAGE}
    USES_TERMINAL
    COMMENT "F-VERIFY: kernel tests under single-CPU THEN -smp 2 (unified AI/CI entry; individuals kept for debug)"
    VERBATIM
)

# F-USABILITY stage 2: boot the Buildroot rootfs.ext2 under the production
# (GUI-off) kernel and gate on the isa-debug-exit code. The rootfs's
# cinux-usability-test.sh (inittab ::once:) runs ls/cat/uname/mkdir/pipe/
# fork-exec, then cinux-exit -> sys_cinux_exit (221) -> port 0xf4 -> QEMU exits
# (code<<1)|1 (0 -> exit 1 = pass, 1 -> exit 3 = fail); qemu_test_wrapper.sh
# maps 1 -> SUCCESS. Build with -DCINUX_GUI=OFF -DCINUX_ROOTFS_PROFILE=buildroot
# -DCINUX_ROOTFS_BUILDROOT_IMG=<rootfs.ext2>.
add_custom_target(run-buildroot-usability
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qemu_test_wrapper.sh
        ${QEMU_EXECUTABLE} ${QEMU_COMMON_FLAGS}
        -device isa-debug-exit,iobase=0xf4,iosize=0x04
        -drive file=${CINUX_IMAGE_PATH},format=raw,index=0,media=disk
        -device ahci,id=ahci
        -drive file=${AHCI_TEST_IMAGE},format=raw,if=none,id=ahci-disk
        -device ide-hd,drive=ahci-disk,bus=ahci.0
        -drive file=${ROOTFS_IMG},format=raw,if=none,id=ext2-disk
        -device ide-hd,drive=ext2-disk,bus=ahci.1
    DEPENDS image ${AHCI_TEST_IMAGE} ${ROOTFS_DEPS}
    USES_TERMINAL
    COMMENT "F-USABILITY: Buildroot rootfs usability gate (init -> test script -> cinux-exit)"
    VERBATIM
)

# F-USABILITY stage 3: assemble the gcc-profile rootfs.ext2 (buildroot base
# target + GCC toolchain closure via tools/gcc-toolchain/extract.sh).  Produces
# a rootfs that ships a native gcc driver so `gcc /hello.c` runs on CinuxOS.
# Assumes the buildroot base target dir (<buildroot>/output/target) already
# exists (built out-of-tree by buildroot); CMake does not track that dependency.
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/rootfs-gcc.ext2
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/assemble_gcc_rootfs.sh
            ${CMAKE_BINARY_DIR}/rootfs-gcc.ext2
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/assemble_gcc_rootfs.sh
            ${CMAKE_SOURCE_DIR}/tools/gcc-toolchain/extract.sh
    VERBATIM
)
add_custom_target(assemble-gcc-rootfs DEPENDS ${CMAKE_BINARY_DIR}/rootfs-gcc.ext2)

# 测试内核调试模式
add_custom_target(run-kernel-test-debug
    COMMAND ${QEMU_EXECUTABLE} ${QEMU_COMMON_FLAGS} ${QEMU_DEBUG_FLAGS}
        -drive file=${CINUX_TEST_IMAGE_PATH},format=raw,index=0,media=disk
    DEPENDS test-image
    COMMENT "Starting QEMU with TEST kernel in debug mode (GDB on :1234)"
    VERBATIM
)

# 交互式运行测试内核（需要 Ctrl+C 退出）
add_custom_target(run-kernel-test-interactive
    COMMAND ${QEMU_EXECUTABLE} ${QEMU_COMMON_FLAGS}
        -drive file=${CINUX_TEST_IMAGE_PATH},format=raw,index=0,media=disk
    DEPENDS test-image
    USES_TERMINAL
    COMMENT "Starting QEMU with TEST kernel (interactive, Ctrl+C to exit)"
    VERBATIM
)

message(STATUS "QEMU targets:")
message(STATUS "  make run        : Start QEMU normally")
message(STATUS "  make run-debug  : Start QEMU with GDB server on :1234")
message(STATUS "  make image      : Build disk image only")
message(STATUS "  make run-gdb    : Connects the qemu automatically")
message(STATUS "")
message(STATUS "Test Kernel targets:")
message(STATUS "  make run-kernel-test            : Run the full big-kernel test suite (auto-exit)")
message(STATUS "  make run-big-kernel-test        : Same suite via the production bootloader (auto-exit)")
message(STATUS "  make run-kernel-test-interactive : Run test kernel (needs Ctrl+C)")
message(STATUS "  make run-kernel-test-debug      : Run test kernel with GDB")
message(STATUS "  make test-image                  : Build test disk image only")
message(STATUS "")
message(STATUS "Stress Test targets:")
message(STATUS "  make stress-kernel-elf  : Generate 1GB synthetic ELF")
message(STATUS "  make stress-test-image  : Build stress test disk image")
message(STATUS "  make run-stress-test    : Run 1GB kernel stress test")
message(STATUS "")
message(STATUS "Unified Testing:")
message(STATUS "  make test                  : Run ALL tests (host + kernel, auto-exit)")
message(STATUS "  make test_host             : Run host unit tests only")
message(STATUS "  make test_verbose          : Run host tests in verbose mode")
