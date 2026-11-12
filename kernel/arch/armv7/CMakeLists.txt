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
    mmu.cpp
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
    -Wl,--no-warn-rwx-segments
    -lgcc)

target_include_directories(arm_image PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(arm_image PROPERTIES
    OUTPUT_NAME "arm"
    SUFFIX ".elf"
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/link.ld)
