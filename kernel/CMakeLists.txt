set(CINUX_ARCHITECTURE "x86_64" CACHE STRING "Architecture directory under kernel/arch")
if(NOT CINUX_ARCHITECTURE STREQUAL "x86_64")
    message(FATAL_ERROR
        "CINUX_ARCHITECTURE='${CINUX_ARCHITECTURE}' has no port under kernel/arch yet; "
        "only x86_64 builds today.")
endif()

file(GLOB_RECURSE CINUX_KERNEL_TREE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/kernel/*.cpp
    ${CMAKE_SOURCE_DIR}/kernel/*.S)

set(CINUX_KERNEL_PORTABLE_SOURCES "")
set(CINUX_KERNEL_ARCH_SOURCES "")
foreach(source IN LISTS CINUX_KERNEL_TREE_SOURCES)
    if(source MATCHES "/arch/")
        if(source MATCHES "/arch/${CINUX_ARCHITECTURE}/")
            list(APPEND CINUX_KERNEL_ARCH_SOURCES ${source})
        endif()
    else()
        list(APPEND CINUX_KERNEL_PORTABLE_SOURCES ${source})
    endif()
endforeach()
list(SORT CINUX_KERNEL_PORTABLE_SOURCES)
list(SORT CINUX_KERNEL_ARCH_SOURCES)

# The boot chain under kernel/arch/<isa>/boot speaks 16- and 32-bit worlds
# of its own; it builds as separate boot targets, never into the kernel.
list(FILTER CINUX_KERNEL_ARCH_SOURCES EXCLUDE REGEX "/arch/${CINUX_ARCHITECTURE}/boot/")

# Packet processing nests on a 16 KiB thread stack. Reject accidental
# connection-control-block temporaries before they corrupt a caller's stack.
set_source_files_properties(${CMAKE_SOURCE_DIR}/kernel/net/tcp.cpp
    PROPERTIES COMPILE_OPTIONS "-Wframe-larger-than=4096")

list(REMOVE_ITEM CINUX_KERNEL_PORTABLE_SOURCES ${CMAKE_SOURCE_DIR}/kernel/kernel.cpp)

set(CINUX_KERNEL_CORE_SOURCES
    ${CINUX_KERNEL_PORTABLE_SOURCES}
    ${CINUX_KERNEL_ARCH_SOURCES}
    ${CMAKE_SOURCE_DIR}/base/src/format/entry.cpp
    ${CMAKE_SOURCE_DIR}/base/src/format/radix.cpp
    ${CMAKE_SOURCE_DIR}/base/src/parse.cpp
    ${CMAKE_SOURCE_DIR}/base/src/path.cpp
    ${CMAKE_SOURCE_DIR}/kernel/lib/freestanding.cpp)

set(CINUX_KERNEL_NO_SSE_ISLAND
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_clock.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/platform/hpet/hpet.cpp
    ${CMAKE_SOURCE_DIR}/kernel/lib/mem.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/isr.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/exception.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/pic.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/irq_stubs.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/platform/apic/local_apic.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/platform/apic/io_apic.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/xhci.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/xhci_layout.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/usb_device.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/usb_keyboard.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/usb_tablet.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/tablet_report.cpp
    ${CMAKE_SOURCE_DIR}/kernel/interrupt/irq.cpp
    ${CMAKE_SOURCE_DIR}/kernel/debug/trace.cpp
    ${CMAKE_SOURCE_DIR}/kernel/debug/kallsyms.cpp
    ${CMAKE_SOURCE_DIR}/kernel/debug/panic.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/irq_guard.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/task_runtime.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/fpu_gate.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/cow.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/cow_resolve.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/demand.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/fault_resolve.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/fork.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/fork_resume.cpp)

set_source_files_properties(${CINUX_KERNEL_NO_SSE_ISLAND} PROPERTIES COMPILE_OPTIONS "-mgeneral-regs-only")

find_program(CINUX_PYTHON_EXECUTABLE python3)
if(NOT CINUX_PYTHON_EXECUTABLE)
    message(FATAL_ERROR "python3 is required to generate the kallsyms table")
endif()

function(cinux_kernel_compile_settings name)
    target_link_libraries(${name} PRIVATE cinux_warnings cinux_kernel_flags)
    target_compile_options(${name} PRIVATE -mcmodel=kernel)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/base/include
        ${CMAKE_SOURCE_DIR})
endfunction()

function(cinux_kernel_link_settings name)
    target_link_options(${name} PRIVATE
        -m64
        -no-pie
        -nostdlib
        -Wl,-T,${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/kernel.ld
        -Wl,--build-id=none
        # The blob objects (font, kallsyms) carry no .note.GNU-stack, so
        # binutils 2.39+ warns an executable stack into every link; the
        # kernel stack is data, never code.
        -Wl,-z,noexecstack
        -Wl,--no-warn-rwx-segments
        -Wl,--gc-sections
        -Wl,-e,KernelEntry)
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/kernel.ld)
endfunction()

function(cinux_kallsyms_bridge bare_target generated_source)
    add_custom_command(
        OUTPUT ${generated_source}
        COMMAND ${CINUX_PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/create/gen_kallsyms.py
                --nm ${CMAKE_NM} --output ${generated_source}
                $<TARGET_FILE:${bare_target}>
        DEPENDS ${bare_target} ${CMAKE_SOURCE_DIR}/scripts/create/gen_kallsyms.py
        VERBATIM)
endfunction()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/font_blob.o
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/assets/font.psf
            ${CMAKE_CURRENT_BINARY_DIR}/font.psf
    COMMAND ${CMAKE_OBJCOPY} -I binary -O elf64-x86-64 -B i386:x86-64
            --rename-section .data=.rodata.font
            --redefine-sym _binary_font_psf_start=g_font_psf_start
            --redefine-sym _binary_font_psf_end=g_font_psf_end
            --set-section-flags .rodata.font=alloc,load,readonly,data
            font.psf
            font_blob.o
    DEPENDS ${CMAKE_SOURCE_DIR}/assets/font.psf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

function(add_cinux_kernel name bin_stem)
    set(kallsyms_source ${CMAKE_CURRENT_BINARY_DIR}/${name}_kallsyms.cpp)
    add_library(${name}_core OBJECT ${CINUX_KERNEL_CORE_SOURCES})
    set_property(TARGET ${name}_core PROPERTY POSITION_INDEPENDENT_CODE OFF)
    cinux_kernel_compile_settings(${name}_core)

    add_executable(${name}_unsym
        ${CMAKE_SOURCE_DIR}/kernel/kernel.cpp
        $<TARGET_OBJECTS:${name}_core>
        ${CMAKE_CURRENT_BINARY_DIR}/font_blob.o
        ${ARGN})
    cinux_kernel_compile_settings(${name}_unsym)
    cinux_kernel_link_settings(${name}_unsym)
    target_compile_definitions(${name}_unsym PRIVATE
        ENTRY_SYMBOL=KernelEntry)

    cinux_kallsyms_bridge(${name}_unsym ${kallsyms_source})

    add_executable(${name}
        ${CMAKE_SOURCE_DIR}/kernel/kernel.cpp
        $<TARGET_OBJECTS:${name}_core>
        ${CMAKE_CURRENT_BINARY_DIR}/font_blob.o
        ${ARGN}
        ${kallsyms_source})
    cinux_kernel_compile_settings(${name})
    cinux_kernel_link_settings(${name})
    target_compile_definitions(${name} PRIVATE
        ENTRY_SYMBOL=KernelEntry)

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/kernel/${bin_stem}.bin
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}>
                ${CMAKE_BINARY_DIR}/kernel/${bin_stem}.bin
        COMMAND ${CMAKE_COMMAND}
                -DIMAGE_FILE=${CMAKE_BINARY_DIR}/kernel/${bin_stem}.bin
                -P ${CMAKE_SOURCE_DIR}/cmake/kernel/image_size_check.cmake
        DEPENDS $<TARGET_FILE:${name}>
                ${CMAKE_SOURCE_DIR}/cmake/kernel/image_size_check.cmake
    )

    add_custom_target(${bin_stem}_bin
        DEPENDS ${CMAKE_BINARY_DIR}/kernel/${bin_stem}.bin
    )
endfunction()

function(add_cinux_ktest name stage)
    set(kallsyms_source ${CMAKE_CURRENT_BINARY_DIR}/cinux_ktest_${name}_kallsyms.cpp)
    set(ktest_core cinux_ktest_${name}_core)
    add_library(${ktest_core} OBJECT EXCLUDE_FROM_ALL ${CINUX_KERNEL_CORE_SOURCES})
    set_property(TARGET ${ktest_core} PROPERTY POSITION_INDEPENDENT_CODE OFF)
    cinux_kernel_compile_settings(${ktest_core})

    add_executable(cinux_ktest_${name}_unsym EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/test/kernel/kernel/test_main.cpp
        $<TARGET_OBJECTS:${ktest_core}>
        ${CMAKE_CURRENT_BINARY_DIR}/font_blob.o
        ${CMAKE_SOURCE_DIR}/test/framework/framework_kernel.cpp
        ${ARGN})
    cinux_kernel_compile_settings(cinux_ktest_${name}_unsym)
    cinux_kernel_link_settings(cinux_ktest_${name}_unsym)
    target_include_directories(cinux_ktest_${name}_unsym PRIVATE
        ${CMAKE_SOURCE_DIR}/test/framework)
    string(TOUPPER ${stage} stage_upper)
    target_compile_definitions(cinux_ktest_${name}_unsym PRIVATE
        ENTRY_SYMBOL=KernelEntry KTEST_STAGE_${stage_upper})

    cinux_kallsyms_bridge(cinux_ktest_${name}_unsym ${kallsyms_source})

    add_executable(cinux_ktest_${name} EXCLUDE_FROM_ALL
        ${CMAKE_SOURCE_DIR}/test/kernel/kernel/test_main.cpp
        $<TARGET_OBJECTS:${ktest_core}>
        ${CMAKE_CURRENT_BINARY_DIR}/font_blob.o
        ${CMAKE_SOURCE_DIR}/test/framework/framework_kernel.cpp
        ${ARGN}
        ${kallsyms_source})
    cinux_kernel_compile_settings(cinux_ktest_${name})
    cinux_kernel_link_settings(cinux_ktest_${name})
    target_include_directories(cinux_ktest_${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/test/framework)
    target_compile_definitions(cinux_ktest_${name} PRIVATE
        ENTRY_SYMBOL=KernelEntry KTEST_STAGE_${stage_upper})

    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/kernel/cinux_ktest_${name}.bin
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:cinux_ktest_${name}>
                ${CMAKE_BINARY_DIR}/kernel/cinux_ktest_${name}.bin
        COMMAND ${CMAKE_COMMAND}
                -DIMAGE_FILE=${CMAKE_BINARY_DIR}/kernel/cinux_ktest_${name}.bin
                -P ${CMAKE_SOURCE_DIR}/cmake/kernel/image_size_check.cmake
        DEPENDS $<TARGET_FILE:cinux_ktest_${name}>
                ${CMAKE_SOURCE_DIR}/cmake/kernel/image_size_check.cmake
    )

    add_custom_target(cinux_ktest_${name}_bin
        DEPENDS ${CMAKE_BINARY_DIR}/kernel/cinux_ktest_${name}.bin
    )
endfunction()

add_cinux_kernel(cinux_kernel kernel)

configure_file(${CMAKE_SOURCE_DIR}/cmake/debug/kernel.gdb.in
               ${CMAKE_BINARY_DIR}/kernel.gdb @ONLY)

foreach(entry IN LISTS CINUX_KTESTS CINUX_KTESTS_INTEGRATION)
    string(REPLACE ":" ";" entry_parts "${entry}")
    list(GET entry_parts 0 ktest_path)
    list(GET entry_parts 1 ktest_stage)
    get_filename_component(ktest "${ktest_path}" NAME)
    string(REPLACE "/" ";" ktest_path_parts "${ktest_path}")
    list(GET ktest_path_parts 0 ktest_module)
    add_cinux_ktest(${ktest} ${ktest_stage}
        ${CMAKE_SOURCE_DIR}/test/kernel/${ktest_module}/test_${ktest}.cpp)
endforeach()
