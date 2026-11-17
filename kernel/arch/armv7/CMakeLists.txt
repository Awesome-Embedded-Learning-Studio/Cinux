# armv7 bring-up image: a freestanding Cortex-A7 binary that QEMU's
# mcimx6ul-evk can load with -kernel.
#
# One core, two images. arm_image is the production kernel: the init lane
# and the desktop. arm_selftest is the self-check kernel the arm_boot lane
# boots. The self-check suite and the seated world compile only into the
# second one, so a production boot carries neither their code nor their
# console noise.

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/font_blob.o
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/assets/font.psf
            ${CMAKE_CURRENT_BINARY_DIR}/font.psf
    COMMAND ${CMAKE_OBJCOPY} -I binary -O elf32-littlearm -B arm
            --rename-section .data=.rodata.font
            --redefine-sym _binary_font_psf_start=g_font_psf_start
            --redefine-sym _binary_font_psf_end=g_font_psf_end
            --set-section-flags .rodata.font=alloc,load,readonly,data
            font.psf
            font_blob.o
    DEPENDS ${CMAKE_SOURCE_DIR}/assets/font.psf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set(ARM_CORE_SOURCES
    start.S
    console_world.cpp
    root_port.cpp
    uart.cpp
    timer.cpp
    gic.cpp
    irq.cpp
    vectors.S
    context_switch.S
    task_switch.cpp
    user_entry.S
    faults.cpp
    exception_dispatch.cpp
    svc.cpp
    lcdif.cpp
    entropy.cpp
    uart_input.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/demand.cpp
    ${CMAKE_SOURCE_DIR}/kernel/loader/loader.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_close.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_getpid.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_getppid.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_lseek.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_open.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/armv7/fork_resume.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_fork.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_clone.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_wait.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_yield.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/fork.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/clone.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/armv7/user_access.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_read.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_exec.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_brk.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_mmap.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_mprotect.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_stat.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_stat_record.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_statx.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_ppoll.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/wait_time.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/fault_resolve.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_pgrp.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_pipe.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_kill.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_signal.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_id.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_uname.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_getcwd.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_getrandom.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_prlimit.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_readlink.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_itimer.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_sendfile.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_msync.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/pipe.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_eventfd.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_openat.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_writev.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_set_tid_address.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_probe.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_fcntl.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_fstat.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_getdents.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_ioctl.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_write.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_munmap.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_common.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_path.cpp
    ${CMAKE_SOURCE_DIR}/kernel/loader/elf_layout.cpp
    ${CMAKE_SOURCE_DIR}/kernel/loader/entry_picture.cpp
    ${CMAKE_SOURCE_DIR}/kernel/loader/exec_arguments.cpp
    ${CMAKE_SOURCE_DIR}/kernel/loader/interpreter.cpp
    ${CMAKE_SOURCE_DIR}/kernel/loader/program_seat.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/framebuffer/framebuffer.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/device/device.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/serial/serial.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/ehci.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/usb_device.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/usb_keyboard.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/usb_tablet.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/tablet_report.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usdhc/usdhc.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/block/block_queue.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/block/block_cache.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/ext2/ext2.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/ext2/ext2_layout.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/ext2/ext2_mutation.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/ext2/ext2_write.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/xhci_layout.cpp
    ${CMAKE_SOURCE_DIR}/kernel/console/console.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/hardware_frames.cpp
    ${CMAKE_SOURCE_DIR}/kernel/console/font.cpp
    ${CMAKE_SOURCE_DIR}/kernel/console/screen.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/waiter.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/kernel_tables.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/block_volume.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/device_tree.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/devfs/console_node.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/devfs/devfs.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/devfs/fb_node.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/devfs/input_node.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/devfs/pty_node.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/input/event_stream.cpp
    fdt.cpp
    firmware.cpp
    vmm_bringup.cpp
    assert_hook.cpp
    user_fs.cpp
    user_seat.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/pmm.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/heap.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/heap_runtime.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/address_space.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/tty/console_tty.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/tty/pty.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/tty/tty.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/ifaces.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/net_world.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/net/loopback.cpp
    ${CMAKE_SOURCE_DIR}/kernel/time/timer_table.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/arp.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/icmp.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/ipv4.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/net_stack.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/net_layout.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/local_socket.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/socket.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/raw.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/tcp.cpp
    ${CMAKE_SOURCE_DIR}/kernel/net/udp.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/file_world.cpp
    ${CMAKE_SOURCE_DIR}/kernel/init/init_thread.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/vfs.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/file_lock.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/ramfs/ramfs.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/procfs/procfs.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/procfs/procfs_init.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/signal_trampoline.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/task_runtime.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_clock.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_chdir.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_sigtimedwait.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_access.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_socket.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_exit.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_nanosleep.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
    ${CMAKE_SOURCE_DIR}/base/src/parse.cpp
    ${CMAKE_SOURCE_DIR}/base/src/path.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
    ${CMAKE_SOURCE_DIR}/kernel/lib/mem.cpp
    ${CMAKE_SOURCE_DIR}/kernel/mm/cow.cpp
    cow_release.cpp
    cow_repair.cpp
    ${CMAKE_SOURCE_DIR}/base/src/format/entry.cpp
    ${CMAKE_SOURCE_DIR}/base/src/format/radix.cpp
    libc_support.cpp
    ${CMAKE_SOURCE_DIR}/kernel/lib/freestanding.cpp)

add_library(arm_core OBJECT ${ARM_CORE_SOURCES})
set_target_properties(arm_core PROPERTIES POSITION_INDEPENDENT_CODE OFF)
target_compile_options(arm_core PRIVATE
    -mcpu=cortex-a7
    -marm
    -mfloat-abi=hard
    -mfpu=vfpv4
    -ffreestanding
    -fno-exceptions
    -fno-rtti
    -fno-pie
    -fno-stack-protector
    -fno-threadsafe-statics
    -Wall
    -Wextra
    -O2
    -g)

target_include_directories(arm_core PRIVATE
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/base/include)

# Both images share the core objects and add their own entry and extras.
# ARGN is the list of sources only one image needs.
function(add_arm_image target output_name)
    add_executable(${target}
        kmain.cpp
        ${ARGN}
        $<TARGET_OBJECTS:arm_core>
        ${CMAKE_CURRENT_BINARY_DIR}/font_blob.o)

    target_compile_options(${target} PRIVATE
        -mcpu=cortex-a7
        -marm
        -mfloat-abi=hard
        -mfpu=vfpv4
        -ffreestanding
        -fno-exceptions
        -fno-rtti
        -fno-pie
        -fno-stack-protector
        -fno-threadsafe-statics
        -Wall
        -Wextra
        -O2
        -g)

    target_link_options(${target} PRIVATE
        -nostdlib
        -no-pie
        -T${CMAKE_CURRENT_SOURCE_DIR}/link.ld
        -Wl,--build-id=none
        -Wl,--no-warn-rwx-segments)

    target_link_libraries(${target} PRIVATE gcc)

    target_include_directories(${target} PRIVATE
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/base/include)

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${output_name}"
        SUFFIX ".elf"
        LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/link.ld)
endfunction()

add_arm_image(arm_image arm)
add_arm_image(arm_selftest arm-selftest selfcheck.cpp seat_probe.cpp seat_prog.S)
target_compile_definitions(arm_selftest PRIVATE CINUX_ARM_SELFTEST=1)

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

# The full-boot door: the same image, but the firmware words say
# kernel_init takes over and pid 1 climbs the card's /sbin/init.
add_custom_command(
    OUTPUT ${CINUX_ARM_INIT_DTB}
    COMMAND ${CINUX_DTC} -I dts -O dtb -o ${CINUX_ARM_INIT_DTB}
            ${CMAKE_CURRENT_SOURCE_DIR}/qemu-init.dts
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qemu-init.dts
    COMMENT "Compiling the full-boot device tree")
add_custom_target(arm_init_dtb DEPENDS ${CINUX_ARM_INIT_DTB})

# The GUI door's firmware words: the same full-boot tree with
# CINUX_SESSION=gui in the environment, so the shared desktop script
# lights the desk. run_gui mounts this variant; the text lane keeps the
# plain one.
add_custom_command(
    OUTPUT ${CINUX_ARM_INIT_GUI_DTB}
    COMMAND ${CINUX_DTC} -I dts -O dtb -o ${CINUX_ARM_INIT_GUI_DTB}
            ${CMAKE_CURRENT_SOURCE_DIR}/qemu-init-gui.dts
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qemu-init-gui.dts
    COMMENT "Compiling the GUI device tree")
add_custom_target(arm_init_gui_dtb DEPENDS ${CINUX_ARM_INIT_GUI_DTB})
add_dependencies(arm_image arm_init_gui_dtb)
add_dependencies(arm_image arm_dtb)
add_dependencies(arm_selftest arm_dtb)
