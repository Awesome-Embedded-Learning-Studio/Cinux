#
# cgui/CMakeLists.txt
#
# @brief cgui -- the host-neutral GUI core (the ONLY hard seam to any host)
#
# cgui_core is a STATIC library of pure C++17: stdint/stddef only, ZERO kernel
# or host includes. The host adapter (kernel/gui/cgui_host_cinux.cpp) is the
# single host-specific TU and is NOT part of this library -- the core never
# depends on it, only the other way around.
#
# This single file builds cgui TWO ways:
#
#   1. As a CinuxOS subdirectory (add_subdirectory(cgui) from the root): the
#      cinux project supplies the freestanding toolchain + flags; cgui_core is
#      linked into the kernel (big_kernel / big_kernel_test). No harness.
#   2. Standalone -- this dir is the build root (`cmake -S cgui -B cgui/build`):
#      it declares its own project under a standard hosted compiler and ALSO
#      builds host/fake_host_main.cpp, a host-neutral smoke harness that drives
#      pump() with a hand-filled table (no kernel, no framebuffer). If
#      that links and runs green off-target, the core is provably host-neutral
#      -- and that harness is the seed of the SDL / MCU host adapters (cgui
#      body is developed against it in a separate project).
#
# The host-neutral core never depends on the adapter, so it can be lifted to
# another repo verbatim: copy cgui/core/ + this CMakeLists.txt + host/.

cmake_minimum_required(VERSION 3.20)

# Declare a project only when this directory IS the build root. As a kernel
# subdirectory the parent (cinux) project is reused and we stay target-only.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(cgui LANGUAGES C CXX)
    set(CGUI_STANDALONE ON)
else()
    set(CGUI_STANDALONE OFF)
endif()

# ============================================================
# host-neutral core library (built in BOTH contexts)
# ============================================================
add_library(cgui_core STATIC
    core/cgui_pump.cpp
    core/cgui_region.cpp
    core/cgui_swraseter.cpp
    core/cgui_abi_check.cpp
)
# core/ is PUBLIC so consumers (the kernel adapter, the harness) resolve the
# bare same-dir core includes via the target, and so the lift set is explicit.
target_include_directories(cgui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_compile_features(cgui_core PUBLIC cxx_std_17)

# ============================================================
# standalone-only: ABI-neutrality smoke harness
# ============================================================
if(CGUI_STANDALONE)
    # Match the kernel's zero-warning discipline (-Wall -Wextra) so the lift set
    # is self-policing; the kernel subdirectory build inherits these from root.
    add_compile_options(-Wall -Wextra)
    enable_testing()
    add_executable(cgui_host_smoke host/fake_host_main.cpp)
    target_link_libraries(cgui_host_smoke PRIVATE cgui_core)
    add_test(NAME cgui_host_smoke COMMAND cgui_host_smoke)
endif()
