find_program(CINUX_PYTHON_EXECUTABLE python3)
if(NOT CINUX_PYTHON_EXECUTABLE)
    message(FATAL_ERROR "Buildroot integration requires Python 3")
endif()

set(CINUX_BUILDROOT_VOLUME "${CMAKE_BINARY_DIR}/boot/native-build.ext2")
set(CINUX_BUILDROOT_VOLUME "${CINUX_BUILDROOT_VOLUME}" PARENT_SCOPE)
# One lane, one defconfig: the architecture names its own file, both carry
# the same shape (busybox init, static devices, shared overlay placeholders).
if(CINUX_ARCHITECTURE STREQUAL "armv7")
    set(_buildroot_defconfig configs/cinux_armv7_defconfig)
else()
    set(_buildroot_defconfig configs/cinux_defconfig)
endif()
set(CINUX_BUILDROOT_DEFCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/${_buildroot_defconfig}"
    CACHE FILEPATH "Cinux Buildroot defconfig")
set(CINUX_BUILDROOT_FRAGMENTS "" CACHE STRING "Ordered Buildroot config fragments (semicolon-separated)")
set(CINUX_BUILDROOT_JOBS 4 CACHE STRING "Parallel jobs for Buildroot packages")
set(_buildroot_command ${CINUX_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/build.py
    --build-dir ${CMAKE_BINARY_DIR} --defconfig ${CINUX_BUILDROOT_DEFCONFIG}
    --arch ${CINUX_ARCHITECTURE}
    --jobs ${CINUX_BUILDROOT_JOBS} --volume ${CINUX_BUILDROOT_VOLUME})
foreach(fragment IN LISTS CINUX_BUILDROOT_FRAGMENTS)
    list(APPEND _buildroot_command --fragment ${fragment})
endforeach()
add_custom_target(buildroot_configure
    COMMAND ${_buildroot_command} --configure-only
    USES_TERMINAL
    VERBATIM)
# The overlay feed: x86 stages the self-host toolchain first (those targets
# live in rootfs.cmake); a terminal lane owns no such targets and prepares
# its overlay directly.
set(_buildroot_overlay_deps "")
if(TARGET toolchain_staging)
    list(APPEND _buildroot_overlay_deps toolchain_staging)
endif()
if(TARGET vesper_desk)
    list(APPEND _buildroot_overlay_deps vesper_desk)
endif()
add_custom_target(native_build_overlay
    COMMAND ${CINUX_PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare.py
            --build-dir ${CMAKE_BINARY_DIR} --overlay-only --arch ${CINUX_ARCHITECTURE}
    DEPENDS ${_buildroot_overlay_deps}
    COMMENT "Cinux sources and native development tools for BR2_ROOTFS_OVERLAY"
    USES_TERMINAL
    VERBATIM)
add_custom_command(
    OUTPUT ${CINUX_BUILDROOT_VOLUME}
    COMMAND ${_buildroot_command} --ensure
    DEPENDS ${_buildroot_overlay_deps}
    COMMENT "Cinux development volume (kept when already present)"
    USES_TERMINAL
    VERBATIM)
add_custom_target(buildroot_volume DEPENDS ${CINUX_BUILDROOT_VOLUME})
add_custom_target(buildroot_rootfs
    COMMAND ${_buildroot_command}
    DEPENDS native_build_overlay boot_image
    COMMENT "Buildroot rootfs with Cinux development overlay"
    USES_TERMINAL
    VERBATIM)
add_custom_target(buildroot_clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_BINARY_DIR}/buildroot/output
    COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_BINARY_DIR}/buildroot/applied.config
    COMMENT "Remove generated Buildroot output before changing its configuration"
    VERBATIM)
