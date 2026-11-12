# armv7 bring-up image: a freestanding Cortex-A7 binary that QEMU's
# mcimx6ul-evk can load with -kernel.

add_executable(arm_image
    start.S
    kmain.cpp
    uart.cpp
    timer.cpp
    gic.cpp
    irq.cpp
    vectors.S
    context_switch.S
    faults.cpp
    exception_dispatch.cpp
    fdt.cpp
    vmm_bringup.cpp
    assert_hook.cpp
    selfcheck.cpp
    console.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/pmm.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/heap.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/heap_runtime.cpp
    ${CMAKE_SOURCE_DIR}/base/src/format/entry.cpp
    ${CMAKE_SOURCE_DIR}/base/src/format/radix.cpp
    libc_support.cpp
    ${CMAKE_SOURCE_DIR}/kernel/lib/freestanding.cpp)

target_compile_options(arm_image PRIVATE
    -mcpu=cortex-a7
    -marm
    -mfloat-abi=hard
    -mfpu=vfpv4
    -ffreestanding
    -fno-exceptions
    -fno-rtti
    -fno-pie
    -fno-stack-protector
    -Wall
    -Wextra
    -O2
    -g)

target_link_options(arm_image PRIVATE
    -nostdlib
    -no-pie
    -T${CMAKE_CURRENT_SOURCE_DIR}/link.ld
    -Wl,--build-id=none
    -Wl,--no-warn-rwx-segments)

target_link_libraries(arm_image PRIVATE gcc)

target_include_directories(arm_image PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/base/include)
set_target_properties(arm_image PROPERTIES
    OUTPUT_NAME "arm"
    SUFFIX ".elf"
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/link.ld)

# The board's device tree: a minimal blob the lane loads at a fixed address.
find_program(CINUX_DTC dtc)
if(NOT CINUX_DTC)
    message(FATAL_ERROR "armv7 lane: dtc not found in PATH")
endif()
add_custom_command(
    OUTPUT ${CINUX_ARM_DTB}
    COMMAND ${CINUX_DTC} -I dts -O dtb -o ${CINUX_ARM_DTB}
            ${CMAKE_CURRENT_SOURCE_DIR}/qemu.dts
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qemu.dts
    COMMENT "Compiling the bring-up device tree")
add_custom_target(arm_dtb DEPENDS ${CINUX_ARM_DTB})
add_dependencies(arm_image arm_dtb)
