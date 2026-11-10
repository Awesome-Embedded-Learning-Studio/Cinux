set(CINUX_QEMU_PROGRAM qemu-system-x86_64 CACHE STRING "QEMU binary driving Cinux")
set(_slirp_wakeup_default OFF)
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(_slirp_wakeup_default ON)
endif()
option(CINUX_QEMU_SLIRP_WAKEUP "Wake the host QEMU main loop after libslirp input"
       ${_slirp_wakeup_default})
set(CINUX_QEMU_COMMAND ${CINUX_QEMU_PROGRAM})
if(CINUX_QEMU_SLIRP_WAKEUP AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    add_library(cinux_qemu_slirp_wakeup SHARED EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_LIST_DIR}/slirp_wakeup.c)
    target_compile_features(cinux_qemu_slirp_wakeup PRIVATE c_std_11)
    target_compile_options(cinux_qemu_slirp_wakeup PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(cinux_qemu_slirp_wakeup PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    set_target_properties(cinux_qemu_slirp_wakeup PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/host-tools)
    set(CINUX_QEMU_COMMAND ${CINUX_PYTHON_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/scripts/mcp/qemu_launch.py ${CINUX_QEMU_PROGRAM}
        $<TARGET_FILE:cinux_qemu_slirp_wakeup>)
    add_dependencies(boot_image cinux_qemu_slirp_wakeup)
endif()
string(REPLACE ";" "\n" _qemu_command_lines "${CINUX_QEMU_COMMAND}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/qemu-command.txt"
    CONTENT "${_qemu_command_lines}\n")

set(CINUX_QEMU_ACCEL auto CACHE STRING
    "QEMU accelerator: auto, kvm, whpx, hvf or tcg")
set_property(CACHE CINUX_QEMU_ACCEL PROPERTY STRINGS auto kvm whpx hvf tcg)

# Ask this QEMU build which accelerators it can initialise. The answer is a
# configure-time snapshot, so upgrading QEMU needs a reconfigure.
execute_process(
    COMMAND ${CINUX_QEMU_PROGRAM} -accel help
    RESULT_VARIABLE _cinux_accel_help_status
    OUTPUT_VARIABLE _cinux_accel_help
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)

# KVM is listed in -accel help even when /dev/kvm is absent or unreadable,
# so the device check is what actually decides. Windows and macOS hosts
# never pass that check, which is fine: their accelerators are WHPX and HVF.
function(_cinux_qemu_accel_usable accel result)
    set(_usable FALSE)
    if(_cinux_accel_help_status EQUAL 0 AND
       _cinux_accel_help MATCHES "(^|\n)${accel}(\r?\n|$)")
        set(_usable TRUE)
    endif()
    if(_usable AND accel STREQUAL "kvm")
        execute_process(COMMAND test -c /dev/kvm -a -w /dev/kvm
                        RESULT_VARIABLE _cinux_kvm_status ERROR_QUIET)
        if(NOT _cinux_kvm_status EQUAL 0)
            set(_usable FALSE)
        endif()
    endif()
    set(${result} ${_usable} PARENT_SCOPE)
endfunction()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(_cinux_accel_order whpx tcg)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(_cinux_accel_order hvf tcg)
else()
    set(_cinux_accel_order kvm tcg)
endif()

set(CINUX_QEMU_ACCEL_SELECTED "")
if(NOT CINUX_QEMU_ACCEL STREQUAL "auto")
    if(NOT CINUX_QEMU_ACCEL MATCHES "^(kvm|whpx|hvf|tcg)$")
        message(FATAL_ERROR
            "CINUX_QEMU_ACCEL=${CINUX_QEMU_ACCEL} is not one of auto, kvm, whpx, hvf, tcg")
    endif()
    _cinux_qemu_accel_usable(${CINUX_QEMU_ACCEL} _cinux_accel_usable)
    if(_cinux_accel_usable)
        set(CINUX_QEMU_ACCEL_SELECTED ${CINUX_QEMU_ACCEL})
    else()
        set(CINUX_QEMU_ACCEL_SELECTED tcg)
        message(WARNING
            "CINUX_QEMU_ACCEL=${CINUX_QEMU_ACCEL} is not usable on this machine; "
            "falling back to tcg (emulated, slow)")
    endif()
else()
    foreach(_cinux_candidate IN LISTS _cinux_accel_order)
        _cinux_qemu_accel_usable(${_cinux_candidate} _cinux_accel_usable)
        if(_cinux_accel_usable)
            set(CINUX_QEMU_ACCEL_SELECTED ${_cinux_candidate})
            break()
        endif()
    endforeach()
    if(CINUX_QEMU_ACCEL_SELECTED STREQUAL "")
        set(CINUX_QEMU_ACCEL_SELECTED tcg)
    endif()
endif()

# Hardware accelerators execute the guest on the real CPU; WHPX and TCG must
# describe a CPU model instead of borrowing the host one. WHPX on real Windows
# aborts with "Unexpected VP exit code 4" when the guest runs with -cpu max
# (seen with QEMU 11.0.1, msys2 build), so keep a conservative model there.
if(CINUX_QEMU_ACCEL_SELECTED STREQUAL "kvm" OR
   CINUX_QEMU_ACCEL_SELECTED STREQUAL "hvf")
    set(CINUX_QEMU_CPU host)
elseif(CINUX_QEMU_ACCEL_SELECTED STREQUAL "whpx")
    set(CINUX_QEMU_CPU qemu64)
else()
    set(CINUX_QEMU_CPU max)
endif()
if(CINUX_QEMU_ACCEL STREQUAL "auto" AND
   CINUX_QEMU_ACCEL_SELECTED STREQUAL "tcg")
    message(STATUS
        "QEMU acceleration: no hardware accelerator usable on ${CMAKE_HOST_SYSTEM_NAME}, "
        "falling back to tcg (emulated, slow)")
else()
    message(STATUS
        "QEMU acceleration: ${CINUX_QEMU_ACCEL_SELECTED} (-cpu ${CINUX_QEMU_CPU})")
endif()

set(CINUX_QEMU_ROOTFS "${CINUX_BUILDROOT_VOLUME}"
    CACHE FILEPATH "Root filesystem used by run, run_debug and run_gui")
set(CINUX_QEMU_MEMORY 1024 CACHE STRING "QEMU runtime memory in MiB")
set(CINUX_QEMU_DEVICE_ARGS
    -accel;${CINUX_QEMU_ACCEL_SELECTED}
    -cpu;${CINUX_QEMU_CPU}
    -smp;2
    -display;none
    -no-reboot
    -device;isa-debug-exit,iobase=0xf4,iosize=0x04
    -device;ahci,id=sata0
    -drive;format=raw,file=${CINUX_EXT2_IMG},if=none,id=spare
    -device;ide-hd,drive=spare,bus=sata0.0
    -device;qemu-xhci,id=xhci0
    -device;usb-kbd,bus=xhci0.0
    -device;usb-tablet,bus=xhci0.0
    -netdev;user,id=net0
    -device;e1000,netdev=net0)
option(CINUX_QEMU_NVME "Attach a scratch NVMe namespace" ON)
if(CINUX_QEMU_NVME)
    set(CINUX_NVME_IMG ${CMAKE_BINARY_DIR}/boot/nvme-scratch.img)
    add_custom_command(OUTPUT ${CINUX_NVME_IMG}
        COMMAND ${CINUX_PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/qemu/create_disk.py
                ${CINUX_NVME_IMG} 8388608
        VERBATIM)
    add_custom_target(qemu_disks DEPENDS ${CINUX_NVME_IMG})
    list(APPEND CINUX_QEMU_DEVICE_ARGS
        -drive;format=raw,file=${CINUX_NVME_IMG},if=none,id=nvme_disk
        -device;nvme,drive=nvme_disk,serial=cinux-nvme)
else()
    add_custom_target(qemu_disks)
endif()
if(TARGET cinux_qemu_slirp_wakeup)
    add_dependencies(qemu_disks cinux_qemu_slirp_wakeup)
endif()
string(REPLACE ";" "\n" CINUX_QEMU_DEVICE_LINES "${CINUX_QEMU_DEVICE_ARGS}")
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/qemu-device-args.txt"
    CONTENT "${CINUX_QEMU_DEVICE_LINES}\n")
