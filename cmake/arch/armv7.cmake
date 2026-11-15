# armv7 lane: bring-up image for Cortex-A7 on QEMU's mcimx6ul-evk.
#
# cinux_arch_toolchain() runs before project() and picks the cross compilers,
# so one configure run carries exactly one toolchain. cinux_arch_lane() runs
# after project(), attaches the bring-up image and its run targets, and marks
# the lane terminal: armv7 has no hosted kernel, no Buildroot rootfs and no
# host unit tests yet, so the root returns.

macro(cinux_arch_toolchain)
    set(CINUX_ARM_TOOLCHAIN_PREFIX "" CACHE STRING
        "Cross compiler file-name prefix for the armv7 lane (empty = sniff PATH)")

    if(NOT CINUX_ARM_TOOLCHAIN_PREFIX)
        set(_cinux_arm_prefix "")
        foreach(_cinux_candidate arm-none-eabi-gcc arm-none-linux-gnueabihf-gcc arm-linux-gnueabihf-gcc)
            find_program(_cinux_arm_gcc ${_cinux_candidate})
            if(_cinux_arm_gcc)
                string(REGEX REPLACE "gcc$" "" _cinux_arm_prefix "${_cinux_arm_gcc}")
                break()
            endif()
        endforeach()
        if(NOT _cinux_arm_prefix)
            message(FATAL_ERROR
                "armv7 lane: no Arm cross compiler found. Install arm-none-eabi-gcc "
                "into PATH, or set CINUX_ARM_TOOLCHAIN_PREFIX to the prefix.")
        endif()
        set(CINUX_ARM_TOOLCHAIN_PREFIX "${_cinux_arm_prefix}")
        message(STATUS "armv7 toolchain sniffed: ${CINUX_ARM_TOOLCHAIN_PREFIX}")
    endif()

    set(CMAKE_SYSTEM_NAME Generic)
    set(CMAKE_SYSTEM_PROCESSOR armv7)
    set(CMAKE_C_COMPILER   ${CINUX_ARM_TOOLCHAIN_PREFIX}gcc)
    set(CMAKE_CXX_COMPILER ${CINUX_ARM_TOOLCHAIN_PREFIX}g++)
    set(CMAKE_ASM_COMPILER ${CINUX_ARM_TOOLCHAIN_PREFIX}gcc)
    # Nothing in this lane links against a hosted libc; a compile-only probe
    # is enough for the toolchain test.
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endmacro()

macro(cinux_arch_lane)
    enable_testing()
    # Shared with the QEMU run targets below; the image build compiles it.
    # The shared kernel tree includes the generated check flags; a terminal
# lane never reaches the hosted options step, so generate them here.
set(CINUX_LOCKDEP_BOOL false)
configure_file(${CMAKE_SOURCE_DIR}/cmake/checks/checks_config.hpp.in
    ${CMAKE_BINARY_DIR}/generated/kernel/proc/checks_config.hpp @ONLY)

set(CINUX_ARM_DTB ${CMAKE_BINARY_DIR}/armv7-qemu.dtb)
# Set here so the parent-scope run targets can name it; the
# subdirectory's custom command compiles it.
set(CINUX_ARM_INIT_DTB ${CMAKE_BINARY_DIR}/armv7-init.dtb)
    add_subdirectory(kernel/arch/armv7)
    include(${CMAKE_SOURCE_DIR}/cmake/qemu/armv7/run.cmake)
    set(CINUX_ARCH_LANE_TERMINAL ON)
endmacro()
