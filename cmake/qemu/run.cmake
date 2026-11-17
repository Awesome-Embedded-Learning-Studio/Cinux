# The spare seat carries the rootfs volume itself; an external ext2
# swaps in by re-pointing CINUX_QEMU_ROOTFS, no per-target rewriting.
set(CINUX_QEMU_RUN_ARGS "${CINUX_QEMU_DEVICE_ARGS}")
list(APPEND CINUX_QEMU_RUN_ARGS -m ${CINUX_QEMU_MEMORY}M)

# Every run target opens a window with the desk on it -- there is no
# console-only lane to keep in step. The transform pairs flags with
# values on purpose: REMOVE_ITEM by value would strip the first
# "-device" word it meets, orphaning that device's option string. The
# gtk window hands the keyboard itself, so the usb keyboard leaves and
# the tablet stays; fw_cfg seeds the session the desktop script reads.
# The ktest lane builds its own headless args from DEVICE_ARGS and
# never passes through this posture.
function(cinux_qemu_window_args source_list out_var)
    set(_window_args "")
    list(LENGTH ${source_list} _window_total)
    set(_window_index 0)
    while(_window_index LESS _window_total)
        list(GET ${source_list} ${_window_index} _window_flag)
        if(_window_flag STREQUAL "-device" OR _window_flag STREQUAL "-display")
            math(EXPR _window_next "${_window_index} + 1")
            list(GET ${source_list} ${_window_next} _window_value)
            if(_window_flag STREQUAL "-device" AND
               NOT _window_value MATCHES "usb-kbd")
                list(APPEND _window_args -device ${_window_value})
            endif()
            math(EXPR _window_index "${_window_index} + 2")
        else()
            list(APPEND _window_args ${_window_flag})
            math(EXPR _window_index "${_window_index} + 1")
        endif()
    endwhile()
    list(APPEND _window_args -display gtk,gl=off,zoom-to-fit=on
        -fw_cfg name=opt/cinux/init-env,string=CINUX_SESSION=gui)
    set(${out_var} ${_window_args} PARENT_SCOPE)
endfunction()

cinux_qemu_window_args(CINUX_QEMU_RUN_ARGS CINUX_QEMU_WINDOW_ARGS)
string(REPLACE ";" "\n" CINUX_QEMU_WINDOW_LINES "${CINUX_QEMU_WINDOW_ARGS}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/qemu-run-args.txt"
    CONTENT "${CINUX_QEMU_WINDOW_LINES}\n")

add_custom_target(run
    COMMAND ${CMAKE_COMMAND} -DROOTFS=${CINUX_QEMU_ROOTFS}
            -P ${CMAKE_SOURCE_DIR}/cmake/qemu/check_rootfs.cmake
    COMMAND ${CINUX_QEMU_COMMAND} ${CINUX_QEMU_WINDOW_ARGS}
            -drive format=raw,file=${CMAKE_BINARY_DIR}/boot/cinux.img
            -serial stdio
    DEPENDS boot_image qemu_disks
    USES_TERMINAL
    VERBATIM
)

add_custom_target(run_debug
    COMMAND ${CMAKE_COMMAND} -DROOTFS=${CINUX_QEMU_ROOTFS}
            -P ${CMAKE_SOURCE_DIR}/cmake/qemu/check_rootfs.cmake
    COMMAND ${CMAKE_COMMAND} -E echo
            "gdb stub on :1234, machine frozen at reset; attach with: gdb -q -x ${CMAKE_BINARY_DIR}/kernel.gdb"
    COMMAND ${CINUX_QEMU_COMMAND} ${CINUX_QEMU_WINDOW_ARGS}
            -drive format=raw,file=${CMAKE_BINARY_DIR}/boot/cinux.img
            -serial stdio -S -s
    DEPENDS boot_image qemu_disks
    USES_TERMINAL
    VERBATIM
)

add_dependencies(test_kernel qemu_disks)
if(CINUX_QEMU_ROOTFS STREQUAL CINUX_BUILDROOT_VOLUME)
    add_dependencies(run buildroot_volume)
    add_dependencies(run_debug buildroot_volume)
endif()

# The card lane: one image carrying the boot chain and the root volume
# together. The card takes the AHCI seat the bare volume used, so the
# kernel's root port finds it with no new wiring, and the spare-drive
# slot disappears from the command line. The volume slot takes whatever
# CINUX_QEMU_ROOTFS names, so an external ext2 image rides straight in;
# the target lives here, after that cache var exists, and stays on
# demand -- the volume may be a heavy buildroot artifact no plain build
# should drag in.
include(${CMAKE_SOURCE_DIR}/cmake/boot/layout_table.cmake)
set(_cinux_boot_dir ${CMAKE_BINARY_DIR}/kernel/arch/x86_64/boot)
add_cinux_layout_image(card_image
    ${CMAKE_BINARY_DIR}/boot/cinux-card.img cinux-card-x86_64.json
    SOURCES mbr=${_cinux_boot_dir}/mbr.bin
            stage2=${_cinux_boot_dir}/stage2.bin
            kernel=${CMAKE_BINARY_DIR}/kernel/kernel.bin
            volume=${CINUX_QEMU_ROOTFS}
    DEPENDS mbr_bin stage2_size_check kernel_bin)
unset(_cinux_boot_dir)

string(REPLACE "${CINUX_QEMU_ROOTFS}" "${CMAKE_BINARY_DIR}/boot/cinux-card.img"
       CINUX_QEMU_CARD_ARGS "${CINUX_QEMU_DEVICE_ARGS}")
list(APPEND CINUX_QEMU_CARD_ARGS -m ${CINUX_QEMU_MEMORY}M)
cinux_qemu_window_args(CINUX_QEMU_CARD_ARGS CINUX_QEMU_CARD_WINDOW_ARGS)
string(REPLACE ";" "\n" CINUX_QEMU_CARD_WINDOW_LINES "${CINUX_QEMU_CARD_WINDOW_ARGS}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/qemu-card-args.txt"
    CONTENT "${CINUX_QEMU_CARD_WINDOW_LINES}\n")

add_custom_target(run_card
    COMMAND ${CMAKE_COMMAND} -DROOTFS=${CINUX_QEMU_ROOTFS}
            -P ${CMAKE_SOURCE_DIR}/cmake/qemu/check_rootfs.cmake
    COMMAND ${CINUX_QEMU_COMMAND} ${CINUX_QEMU_CARD_WINDOW_ARGS}
            -serial stdio
    DEPENDS card_image qemu_disks
    USES_TERMINAL
    VERBATIM
)

add_custom_target(run_card_debug
    COMMAND ${CMAKE_COMMAND} -DROOTFS=${CINUX_QEMU_ROOTFS}
            -P ${CMAKE_SOURCE_DIR}/cmake/qemu/check_rootfs.cmake
    COMMAND ${CMAKE_COMMAND} -E echo
            "gdb stub on :1234, machine frozen at reset; attach with: gdb -q -x ${CMAKE_BINARY_DIR}/kernel.gdb"
    COMMAND ${CINUX_QEMU_COMMAND} ${CINUX_QEMU_CARD_WINDOW_ARGS}
            -serial stdio -S -s
    DEPENDS card_image qemu_disks
    USES_TERMINAL
    VERBATIM
)
if(CINUX_QEMU_ROOTFS STREQUAL CINUX_BUILDROOT_VOLUME)
    add_dependencies(card_image buildroot_volume)
endif()
