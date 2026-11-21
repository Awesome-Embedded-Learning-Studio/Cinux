# Every run target boots the single card image -- the boot chain and the
# root volume baked into one img. The console lane keeps the raw device
# args (display none, serial stdio); the gui lane swaps in a GTK window.
# The window transform pairs flags with values on purpose: REMOVE_ITEM
# by value would strip the first "-device" word it meets, orphaning that
# device's option string. The gtk window hands the keyboard itself, so
# the usb keyboard leaves and the tablet stays; fw_cfg seeds the session
# the desktop script reads. The ktest lane builds its own headless args
# from DEVICE_ARGS and never passes through this posture.
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

# The card takes the AHCI seat the bare volume used, so the kernel's
# root port finds it with no new wiring, and the spare-drive slot
# disappears from the command line. The volume slot takes whatever
# CINUX_QEMU_ROOTFS names, so an external ext2 image rides straight in;
# the target stays on demand -- the volume may be a heavy buildroot
# artifact no plain build should drag in.
include(${CMAKE_SOURCE_DIR}/cmake/boot/layout_table.cmake)
set(_cinux_boot_dir ${CMAKE_BINARY_DIR}/kernel/arch/x86_64/boot)
add_cinux_layout_image(card_image cinux-x86_64.json
    SOURCES mbr=${_cinux_boot_dir}/mbr.bin
            stage2=${_cinux_boot_dir}/stage2.bin
            kernel=${CMAKE_BINARY_DIR}/kernel/kernel.bin
            volume=${CINUX_QEMU_ROOTFS}
    DEPENDS mbr_bin stage2_size_check kernel_bin)
unset(_cinux_boot_dir)

string(REPLACE "${CINUX_QEMU_ROOTFS}" "${CMAKE_BINARY_DIR}/boot/cinux.img"
       CINUX_QEMU_CARD_ARGS "${CINUX_QEMU_DEVICE_ARGS}")
list(APPEND CINUX_QEMU_CARD_ARGS -m ${CINUX_QEMU_MEMORY}M)
string(REPLACE ";" "\n" CINUX_QEMU_CARD_LINES "${CINUX_QEMU_CARD_ARGS}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/qemu-run-args.txt"
    CONTENT "${CINUX_QEMU_CARD_LINES}\n")

cinux_qemu_window_args(CINUX_QEMU_CARD_ARGS CINUX_QEMU_CARD_WINDOW_ARGS)
string(REPLACE ";" "\n" CINUX_QEMU_CARD_WINDOW_LINES "${CINUX_QEMU_CARD_WINDOW_ARGS}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/qemu-gui-args.txt"
    CONTENT "${CINUX_QEMU_CARD_WINDOW_LINES}\n")

# The ecosystem lane swaps the development volume for the test volume on
# the spare AHCI seat; everything else (nvme scratch, network, usb) stays
# identical to the device args the run lane sees.
if(TARGET eco_img)
    string(REPLACE "${CINUX_QEMU_ROOTFS}" "${CMAKE_BINARY_DIR}/boot/eco.img"
           CINUX_QEMU_ECO_ARGS "${CINUX_QEMU_DEVICE_ARGS}")
    string(REPLACE ";" "\n" CINUX_QEMU_ECO_LINES "${CINUX_QEMU_ECO_ARGS}")
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/qemu-eco-args.txt"
        CONTENT "${CINUX_QEMU_ECO_LINES}\n")

    # The guest lane as plain ctest tests: every boot rides -snapshot on
    # the eco volume, so the tests share the card read-only and a
    # parallel `ctest -R '^guest_' -j` is safe. The test_guest target
    # carries every input; ctest itself never builds.
    function(cinux_add_guest_test name script)
        add_test(NAME guest_${name}
            COMMAND ${CMAKE_COMMAND} -E env TMPDIR=${CMAKE_BINARY_DIR}
                    ${CINUX_PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/${script} ${ARGN})
        set_tests_properties(guest_${name} PROPERTIES
            TIMEOUT 420 LABELS "guest;${name}")
    endfunction()
    cinux_add_guest_test(ecosystem test_ecosystem.py
        --image ${CMAKE_BINARY_DIR}/boot/cinux-boot.img)
    cinux_add_guest_test(selfhost test_selfhost.py
        --image ${CMAKE_BINARY_DIR}/boot/cinux-boot.img)
    cinux_add_guest_test(net_ping test_net_ping.py
        --image ${CMAKE_BINARY_DIR}/boot/cinux-boot.img
        --log ${CMAKE_BINARY_DIR}/net-ping.log)
    cinux_add_guest_test(net_tcp test_net_tcp.py
        --build-dir ${CMAKE_BINARY_DIR} --log-dir ${CMAKE_BINARY_DIR}/tcp-smoke)
    cinux_add_guest_test(desktop test_desktop.py
        --image ${CMAKE_BINARY_DIR}/boot/cinux-boot.img)
    add_custom_target(test_guest DEPENDS boot_image eco_img qemu_disks)
endif()

function(cinux_add_run_target name extra_args args_list)
    add_custom_target(${name}
        COMMAND ${CMAKE_COMMAND} -DROOTFS=${CINUX_QEMU_ROOTFS}
                -P ${CMAKE_SOURCE_DIR}/cmake/qemu/check_rootfs.cmake
        COMMAND ${CINUX_QEMU_COMMAND} ${${args_list}}
                -serial stdio ${extra_args}
        DEPENDS card_image qemu_disks
        USES_TERMINAL
        VERBATIM)
endfunction()

cinux_add_run_target(run "" CINUX_QEMU_CARD_ARGS)
cinux_add_run_target(run_debug "-S -s" CINUX_QEMU_CARD_ARGS)
cinux_add_run_target(run_gui "" CINUX_QEMU_CARD_WINDOW_ARGS)
cinux_add_run_target(run_gui_debug "-S -s" CINUX_QEMU_CARD_WINDOW_ARGS)

add_dependencies(test_kernel qemu_disks)
if(CINUX_QEMU_ROOTFS STREQUAL CINUX_BUILDROOT_VOLUME)
    add_dependencies(card_image buildroot_volume)
endif()
