# armv7 bring-up image: a freestanding Cortex-A7 binary that QEMU's
# mcimx6ul-evk can load with -kernel.

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

set(USERPROG_SOURCE ${CMAKE_SOURCE_DIR}/assets/armv7_userprog.elf)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/userprog_blob.o
    COMMAND ${CMAKE_COMMAND} -E copy ${USERPROG_SOURCE} userprog.elf
    COMMAND ${CMAKE_OBJCOPY} -I binary -O elf32-littlearm -B arm
            --rename-section .data=.rodata.userprog
            --redefine-sym _binary_userprog_elf_start=g_userprog_start
            --redefine-sym _binary_userprog_elf_end=g_userprog_end
            --set-section-flags .rodata.userprog=alloc,load,readonly,data
            userprog.elf
            userprog_blob.o
    DEPENDS ${USERPROG_SOURCE}
    COMMENT "objcopy userprog blob")

add_executable(arm_image
    start.S
    kmain.cpp
    uart.cpp
    timer.cpp
    gic.cpp
    irq.cpp
    vectors.S
    context_switch.S
    task_switch.cpp
    user_entry.S
    selfcheck.cpp
    faults.cpp
    exception_dispatch.cpp
    svc.cpp
    lcdif.cpp
    entropy.cpp
    seat_prog.S
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
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_wait.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_yield.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/fork.cpp
    ${CMAKE_SOURCE_DIR}/kernel/arch/armv7/user_access.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_read.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_exec.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_brk.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_mmap.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_mprotect.cpp
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
    ${CMAKE_CURRENT_BINARY_DIR}/font_blob.o
    ${CMAKE_CURRENT_BINARY_DIR}/userprog_blob.o
    ${CMAKE_SOURCE_DIR}/kernel/drivers/framebuffer/framebuffer.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/device/device.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/serial/serial.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/ehci.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/usb_device.cpp
    ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/usb_keyboard.cpp
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
    selfcheck.cpp
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
    ${CMAKE_SOURCE_DIR}/kernel/fs/vfs.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/file_lock.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/ramfs/ramfs.cpp
    ${CMAKE_SOURCE_DIR}/kernel/fs/procfs/procfs.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/signal_trampoline.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/task_runtime.cpp
    ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
    ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_clock.cpp
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
    -fno-threadsafe-statics
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
