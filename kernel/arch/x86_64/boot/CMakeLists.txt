include(${CMAKE_SOURCE_DIR}/cmake/boot/boot_target.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/boot/layout_table.cmake)

add_executable(mbr mbr.cpp)
add_cinux_boot_binary(mbr 16 mbr.ld -Wl,--gc-sections)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/layout.hpp layout_src)
string(REGEX MATCH "\\.sectors = ([0-9]+)" layout_match "${layout_src}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/layout.hpp)

add_library(boot_pm OBJECT
    pm/pm.cpp
    lm/lm_switch.cpp
    lm/page_tables.cpp
    load/ferry32.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/platform/io.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/serial/serial.cpp)
target_link_libraries(boot_pm PRIVATE cinux_warnings cinux_boot32)
target_include_directories(boot_pm PRIVATE
    ${CMAKE_SOURCE_DIR}/base/include
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(lm_blob lm/lm.cpp ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/platform/io.cpp ${CMAKE_SOURCE_DIR}/kernel/drivers/serial/serial.cpp)
add_cinux_boot_binary(lm_blob 64 lm/lm.ld)
target_compile_options(lm_blob PRIVATE -mcmodel=small)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lm_blob.tmp.o
           ${CMAKE_CURRENT_BINARY_DIR}/lm_blob.o
    COMMAND ${CMAKE_OBJCOPY} -I binary -O elf32-i386 -B i386
            --rename-section .data=.lm_blob
            ${CMAKE_CURRENT_BINARY_DIR}/lm_blob.bin
            ${CMAKE_CURRENT_BINARY_DIR}/lm_blob.tmp.o
    COMMAND ${CMAKE_OBJCOPY} --set-section-flags .lm_blob=alloc,load,readonly,data
            ${CMAKE_CURRENT_BINARY_DIR}/lm_blob.tmp.o
            ${CMAKE_CURRENT_BINARY_DIR}/lm_blob.o
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/lm_blob.bin)
add_custom_target(lm_blob_obj
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/lm_blob.o)

add_executable(stage2
    stage2.cpp
    bios.cpp
    gdt/gdt.cpp
    pm/pm_switch.cpp
    load/ferry.cpp
    load/loader.cpp
    ${CMAKE_SOURCE_DIR}/kernel/lib/freestanding.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/lm_blob.o
    ${CMAKE_SOURCE_DIR}/base/src/format/entry.cpp
    ${CMAKE_SOURCE_DIR}/base/src/format/radix.cpp
    $<TARGET_OBJECTS:boot_pm>)
add_cinux_boot_binary(stage2 16 stage2.ld -Wl,--gc-sections)
target_compile_options(mbr PRIVATE -ffunction-sections)
target_include_directories(stage2 PRIVATE ${CMAKE_SOURCE_DIR}/base/src)
target_compile_options(stage2 PRIVATE -ffunction-sections)
add_dependencies(stage2 lm_blob_obj)

add_custom_target(stage2_size_check ALL
    COMMAND ${CMAKE_COMMAND}
            -DSIZE_FILE=${CMAKE_CURRENT_BINARY_DIR}/stage2.bin
            -DMAX_SECTORS=${CMAKE_MATCH_1}
            -P ${CMAKE_SOURCE_DIR}/cmake/boot/stage2_size_check.cmake
    DEPENDS stage2_bin
)

# Every offset and size decision lives in scripts/meta/cinux-boot-x86_64.json;
# this wiring only feeds the artifacts in. The ktest images ride the same
# table through the same function.
function(add_cinux_image name image_file kernel_bin kernel_dep)
    add_cinux_layout_image(${name} ${image_file} cinux-boot-x86_64.json
        SOURCES mbr=${CMAKE_CURRENT_BINARY_DIR}/mbr.bin
                stage2=${CMAKE_CURRENT_BINARY_DIR}/stage2.bin
                kernel=${kernel_bin}
        DEPENDS mbr_bin stage2_size_check ${kernel_dep})
endfunction()

add_cinux_image(boot_image
    ${CMAKE_BINARY_DIR}/boot/cinux.img
    ${CMAKE_BINARY_DIR}/kernel/kernel.bin kernel_bin)

foreach(entry IN LISTS CINUX_KTESTS CINUX_KTESTS_INTEGRATION)
    string(REPLACE ":" ";" entry_parts "${entry}")
    list(GET entry_parts 0 ktest_path)
    get_filename_component(ktest "${ktest_path}" NAME)
    add_cinux_image(boot_ktest_${ktest}
        ${CMAKE_BINARY_DIR}/boot/cinux_ktest_${ktest}.img
        ${CMAKE_BINARY_DIR}/kernel/cinux_ktest_${ktest}.bin
        cinux_ktest_${ktest}_bin)
endforeach()


add_custom_target(cinux_image ALL DEPENDS boot_image)
