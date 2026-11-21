set(CINUX_BUSYBOX_GLIBC ${CMAKE_BINARY_DIR}/busybox-glibc/busybox)
set(CINUX_HELLO_GLIBC ${CMAKE_BINARY_DIR}/glibc/hello-glibc)
add_custom_command(
    OUTPUT ${CINUX_BUSYBOX_GLIBC}
    COMMAND ${CMAKE_COMMAND} -E env
            "CINUX_ROOTFS_CACHE=${CMAKE_SOURCE_DIR}/.cache/rootfs"
            bash ${CMAKE_SOURCE_DIR}/scripts/create/build-busybox-glibc.sh ${CINUX_BUSYBOX_GLIBC}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/create/build-busybox-glibc.sh
            ${CMAKE_SOURCE_DIR}/user/rootfs/busybox/busybox.config
    COMMENT "glibc busybox for the ecosystem volume")
add_custom_target(busybox_glibc DEPENDS ${CINUX_BUSYBOX_GLIBC})
add_library(selfhost_seed_checks OBJECT EXCLUDE_FROM_ALL
    scripts/create/ext2_seed/hello.cpp scripts/create/ext2_seed/execprobe.c scripts/create/ext2_seed/sendfileprobe.c scripts/create/ext2_seed/nativeprobe.c)
target_link_libraries(selfhost_seed_checks PRIVATE cinux_warnings)
set_source_files_properties(scripts/create/ext2_seed/nativeprobe.c PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
set(CINUX_TOOLCHAIN_STAGING ${CMAKE_BINARY_DIR}/toolchain-staging)
add_custom_command(
    OUTPUT ${CINUX_TOOLCHAIN_STAGING}/hello.s
    COMMAND ${CMAKE_COMMAND} -E env
            "CINUX_ROOTFS_CACHE=${CMAKE_SOURCE_DIR}/.cache/rootfs"
            bash ${CMAKE_SOURCE_DIR}/scripts/create/build-toolchain-staging.sh ${CINUX_TOOLCHAIN_STAGING}
    DEPENDS selfhost_seed_checks
            ${CMAKE_SOURCE_DIR}/scripts/create/build-toolchain-staging.sh
            ${CMAKE_SOURCE_DIR}/scripts/create/ext2_seed/hello.c
            ${CMAKE_SOURCE_DIR}/scripts/create/ext2_seed/hello.cpp
            ${CMAKE_SOURCE_DIR}/scripts/create/ext2_seed/execprobe.c
            ${CMAKE_SOURCE_DIR}/scripts/create/ext2_seed/sendfileprobe.c
    COMMENT "GCC/G++ toolchain staging for the self-host volume")
add_custom_target(toolchain_staging DEPENDS ${CINUX_TOOLCHAIN_STAGING}/hello.s)
add_custom_command(
    OUTPUT ${CINUX_HELLO_GLIBC}
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/create/glibc/build-hello-glibc.sh ${CINUX_HELLO_GLIBC}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/create/glibc/build-hello-glibc.sh
    COMMENT "glibc hello for the ecosystem volume")
add_custom_target(hello_glibc DEPENDS ${CINUX_HELLO_GLIBC})
set(CINUX_VESPER_ROOT "${CMAKE_SOURCE_DIR}/third_party/vesper")
file(GLOB_RECURSE CINUX_VESPER_INPUTS CONFIGURE_DEPENDS
    "${CINUX_VESPER_ROOT}/src/*.cpp" "${CINUX_VESPER_ROOT}/src/*.hpp"
    "${CINUX_VESPER_ROOT}/apps/terminal/*.cpp" "${CINUX_VESPER_ROOT}/apps/terminal/*.hpp"
    "${CINUX_VESPER_ROOT}/assets/*.hpp"
    "${CINUX_VESPER_ROOT}/cmake/*.cmake"
    "${CINUX_VESPER_ROOT}/third_party/visor/src/*.cpp"
    "${CINUX_VESPER_ROOT}/third_party/visor/include/*.hpp")
set(CINUX_VESPER_DESK ${CMAKE_BINARY_DIR}/glibc/vesper-desk)
add_custom_command(
    OUTPUT ${CINUX_VESPER_DESK}
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/create/build-vesper-desk.sh ${CINUX_VESPER_DESK}
            ${CMAKE_BINARY_DIR}/third_party/vesper ${CINUX_VESPER_ROOT}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/create/build-vesper-desk.sh
            ${CINUX_VESPER_ROOT}/CMakeLists.txt
            ${CINUX_VESPER_ROOT}/examples/CMakeLists.txt
            ${CINUX_VESPER_ROOT}/apps/terminal/CMakeLists.txt
            ${CINUX_VESPER_ROOT}/examples/fbdev_desktop.cpp
            ${CINUX_VESPER_INPUTS}
    COMMENT "vesper fbdev desk, static")
add_custom_target(vesper_desk DEPENDS ${CINUX_VESPER_DESK})
# The scratch volume the kernel tests mount. The ext2 ktests assert
# against bytes baked here (hello.txt) and grow files at runtime, so
# this image belongs to the test world, not to any run lane, and stays
# small enough that per-test copies are cheap.
set(CINUX_KTEST_FIXTURE_DIR ${CMAKE_BINARY_DIR}/ktest-fixture)
file(MAKE_DIRECTORY ${CINUX_KTEST_FIXTURE_DIR}/etc)
file(COPY ${CMAKE_SOURCE_DIR}/test/kernel/fs/fixtures/hello.txt
     DESTINATION ${CINUX_KTEST_FIXTURE_DIR})
set(CINUX_EXT2_KTEST_IMG ${CMAKE_BINARY_DIR}/boot/ext2_ktest.img)
add_custom_command(
    OUTPUT ${CINUX_EXT2_KTEST_IMG}
    # The fixture must not lean on another target having created boot/
    # first: on a fresh build directory the parallel graph can schedule
    # this command before anything else touches the directory.
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/boot
    COMMAND ${CMAKE_COMMAND} -E rm -f ${CINUX_EXT2_KTEST_IMG}
    COMMAND mke2fs -q -t ext2 -b 1024 -O none
            -d ${CINUX_KTEST_FIXTURE_DIR}
            ${CINUX_EXT2_KTEST_IMG} 16384k
    DEPENDS ${CINUX_KTEST_FIXTURE_DIR}/hello.txt
    COMMENT "ktest ext2 fixture volume")
add_custom_target(ext2_ktest_img DEPENDS ${CINUX_EXT2_KTEST_IMG})

# The ecosystem test volume: busybox, the glibc runtime and the guest-side
# test binaries layered over the self-host staging tree. The station33/34
# checks boot the kernel-only image against this volume (wired through
# qemu-eco-args.txt in cmake/qemu/run.cmake), keeping the run/run_gui
# development volume free of test freight.
set(CINUX_ECO_VOLUME ${CMAKE_BINARY_DIR}/boot/eco.img)
add_custom_command(
    OUTPUT ${CINUX_ECO_VOLUME}
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/create/build-eco-volume.sh
            ${CINUX_ECO_VOLUME} ${CMAKE_BINARY_DIR}
    DEPENDS busybox_glibc hello_glibc user_ecotest toolchain_staging vesper_desk
            user_hello user_ttydemo user_smpstress
            ${CMAKE_SOURCE_DIR}/scripts/create/ext2_seed/smp-stress.sh
            ${CMAKE_SOURCE_DIR}/scripts/create/build-eco-volume.sh
    COMMENT "ecosystem test volume (busybox + selfhost staging + test binaries)")
add_custom_target(eco_img DEPENDS ${CINUX_ECO_VOLUME})

# The buildroot lane loads before this file (it must precede the
# terminal-lane return), so its configure-time DEPENDS on
# toolchain_staging / vesper_desk saw empty lists. Wire the overlay
# feed here, once both sides exist, whichever loads first.
foreach(_buildroot_overlay_target native_build_overlay buildroot_volume buildroot_rootfs)
    if(TARGET ${_buildroot_overlay_target})
        add_dependencies(${_buildroot_overlay_target} toolchain_staging vesper_desk)
    endif()
endforeach()
