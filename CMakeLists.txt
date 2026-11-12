# We Need CMake Acquired above 3.16 :)
cmake_minimum_required(VERSION 3.16)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/platform")

# Architecture is a configure-time choice. Each architecture lives in one
# file under cmake/arch/ providing cinux_arch_toolchain() (runs before
# project(), picks the compilers) and cinux_arch_lane() (runs after,
# attaches sources and run targets, and may own the rest of the configure).
set(CINUX_ARCHITECTURE "x86_64" CACHE STRING
    "Target architecture; one file per entry under cmake/arch/")
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arch/${CINUX_ARCHITECTURE}.cmake")
    message(FATAL_ERROR "No cmake/arch/${CINUX_ARCHITECTURE}.cmake; unknown CINUX_ARCHITECTURE")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/arch/${CINUX_ARCHITECTURE}.cmake")
cinux_arch_toolchain()

project(Cinux VERSION 0.1.0 LANGUAGES C CXX ASM)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
        message(FATAL_ERROR
            "Cinux requires GCC >= 14 (C++23 freestanding + strict warnings); "
            "found GCC ${CMAKE_CXX_COMPILER_VERSION}.")
    endif()
endif()

# We Forced Acquired C++23, Light Weight And fun enough!
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Extensions off :)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# compile commands could help us start our journey, happily
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The lane attaches its sources and run targets here. A lane that owns the
# rest of the configure (no hosted kernel, no Buildroot rootfs, no host
# tests) sets CINUX_ARCH_LANE_TERMINAL and the root returns.
# One generated shim per neutral arch header, pointing at the selected
# port's implementation. Neutral headers include "cinux/port/<name>.hpp",
# so a port coexists instead of editing shared files.
set(CINUX_PORT_HEADERS
    abi context cpuid debug_frame firmware halt init instructions irq_guard isr
    kernel_stack msr page page_entry page_walk_shape per_cpu per_cpu_config random
    smp_control space_control space_layout syscall_abi task_context user_access user_entry)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated/cinux/port)
foreach(port_header IN LISTS CINUX_PORT_HEADERS)
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/generated/cinux/port/${port_header}.hpp
         CONTENT "#pragma once\n#include \"kernel/arch/${CINUX_ARCHITECTURE}/${port_header}.hpp\"\n")
endforeach()

# Same shim story for the platform face: a neutral platform header names no
# port, the generated file points at the selected port's aggregate.
set(CINUX_PLATFORM_HEADERS bus clock firmware interrupt io ps2)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/generated/cinux/platform)
foreach(platform_header IN LISTS CINUX_PLATFORM_HEADERS)
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/generated/cinux/platform/${platform_header}.hpp
         CONTENT "#pragma once\n#include \"kernel/arch/${CINUX_ARCHITECTURE}/platform/${platform_header}.hpp\"\n")
endforeach()

cinux_arch_lane()
if(CINUX_ARCH_LANE_TERMINAL)
    return()
endif()

include(cmake/compilers/warning_standard.cmake)
include(cmake/compilers/boot_modes.cmake)
include(cmake/compilers/kernel_modes.cmake)
include(cmake/checks/options.cmake)

find_program(CINUX_PYTHON_EXECUTABLE python3)
if(NOT CINUX_PYTHON_EXECUTABLE)
    message(FATAL_ERROR "Cinux build tools require Python 3")
endif()
set(CINUX_COMPILE_JOBS 2 CACHE STRING "Ninja compile pool size")
set(CINUX_LINK_JOBS 1 CACHE STRING "Ninja link pool size")
set_property(GLOBAL PROPERTY JOB_POOLS cinux_compile=${CINUX_COMPILE_JOBS} cinux_link=${CINUX_LINK_JOBS})
set(CMAKE_JOB_POOL_COMPILE cinux_compile)
set(CMAKE_JOB_POOL_LINK cinux_link)

include(cmake/test/kernel_catalog.cmake)
add_subdirectory(kernel/arch/x86_64/boot)
add_subdirectory(base)
add_subdirectory(user)
add_subdirectory(kernel)
include(cmake/rootfs.cmake)
add_subdirectory(user/rootfs/buildroot)
include(cmake/qemu/devices.cmake)
add_subdirectory(test EXCLUDE_FROM_ALL)
include(cmake/qemu/run.cmake)
