enable_testing()

set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/test/framework
    ${CMAKE_SOURCE_DIR}/base/include
    ${CMAKE_SOURCE_DIR}
)

# Capability tests mirror the kernel's module tree: unit/<module>/test_<name>.cpp.
# A capability test proves what the OS does, so it may only reach the neutral
# headers; the arch boundary check below enforces that.
function(add_cinux_test spec)
    string(REPLACE "/" ";" parts "${spec}")
    list(GET parts 0 module)
    list(GET parts 1 name)
    add_executable(test_${name}
        unit/${module}/test_${name}.cpp
        ${ARGN}
        framework/framework.cpp
        framework/assert_host.cpp
        framework/console_host.cpp
        framework/per_cpu_host.cpp
        framework/irq_guard_host.cpp)
    target_include_directories(test_${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(test_${name} PRIVATE cinux_base cinux_warnings)
    add_test(NAME ${name} COMMAND test_${name})
    set_tests_properties(${name} PROPERTIES LABELS "${module};capability")
    list(APPEND ALL_HOST_TESTS test_${name})
    set(ALL_HOST_TESTS ${ALL_HOST_TESTS} PARENT_SCOPE)
endfunction()

# Architecture tests assert ISA machinery and may reach into the port's own
# headers; they live under arch/<isa>/ and are rewritten with the port.
function(add_cinux_arch_test name)
    add_executable(test_${name}
        arch/${CINUX_ARCHITECTURE}/test_${name}.cpp
        ${ARGN}
        framework/framework.cpp
        framework/assert_host.cpp
        framework/console_host.cpp
        framework/per_cpu_host.cpp
        framework/irq_guard_host.cpp)
    target_include_directories(test_${name} PRIVATE ${TEST_INCLUDE_DIRS})
    target_link_libraries(test_${name} PRIVATE cinux_base cinux_warnings)
    add_test(NAME ${name} COMMAND test_${name})
    set_tests_properties(${name} PROPERTIES LABELS "arch")
    list(APPEND ALL_HOST_TESTS test_${name})
    set(ALL_HOST_TESTS ${ALL_HOST_TESTS} PARENT_SCOPE)
endfunction()

add_cinux_arch_test(e820)
add_cinux_test(base/format)
add_cinux_test(base/framework)
add_cinux_test(base/result)
add_cinux_arch_test(vesa)
add_cinux_test(base/bits)
add_cinux_test(base/checksum)
add_cinux_test(net/net_layout ${CMAKE_SOURCE_DIR}/kernel/net/net_layout.cpp)
add_cinux_test(net/net_arp ${CMAKE_SOURCE_DIR}/kernel/net/arp.cpp
                         ${CMAKE_SOURCE_DIR}/kernel/net/net_layout.cpp)
add_cinux_test(net/net_stack ${CMAKE_SOURCE_DIR}/kernel/net/net_stack.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/net/net_layout.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/net/ipv4.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/net/icmp.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/drivers/net/loopback.cpp)
add_cinux_test(net/net_udp ${CMAKE_SOURCE_DIR}/kernel/net/udp.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/net/net_layout.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/net/ipv4.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/net/icmp.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/net/net_stack.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/drivers/net/loopback.cpp)
add_cinux_test(net/net_tcp ${CMAKE_SOURCE_DIR}/kernel/net/tcp.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/net/net_layout.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/net/ipv4.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/net/icmp.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/net/net_stack.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/drivers/net/loopback.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/time/timer_table.cpp)
add_cinux_test(net/net_socket ${CMAKE_SOURCE_DIR}/kernel/net/raw.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/net/local_socket.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_itimer.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/net/socket.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/net/udp.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/net/tcp.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/net/net_layout.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/net/ipv4.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/net/icmp.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/net/arp.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/net/net_stack.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/net/net_world.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/drivers/net/loopback.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_socket.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_ioctl.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/net/ifaces.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/drivers/tty/tty.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_common.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_path.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/time/timer_table.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
add_cinux_arch_test(gdt)
add_cinux_arch_test(idt)
add_cinux_arch_test(layout)
add_cinux_test(mm/mm_layout)
add_cinux_test(mm/vmm)
add_cinux_test(mm/page)
add_cinux_arch_test(cpu_bits)
add_cinux_arch_test(firmware_config ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/firmware_config.cpp
                               mock/firmware_config.cpp)
add_cinux_arch_test(extable)
add_cinux_test(base/random)
add_cinux_test(syscall/uaccess ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                         ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                         ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
                         ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                         ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                         ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                         ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp
                         ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                         ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                         ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
add_cinux_test(proc/sched_row ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
add_cinux_test(loader/image)
add_cinux_test(base/bitmap)
add_cinux_test(time/time)
add_cinux_test(time/timer ${CMAKE_SOURCE_DIR}/kernel/time/timer_table.cpp)
add_cinux_test(base/path)
add_cinux_test(base/text)
add_cinux_test(drivers/rtc)
add_cinux_arch_test(hpet)
add_cinux_test(drivers/nvme)
add_cinux_arch_test(madt ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/platform/acpi/madt.cpp)
add_cinux_test(drivers/font)
add_cinux_test(drivers/console)
add_cinux_test(syscall/syscall_face ${CMAKE_SOURCE_DIR}/kernel/net/ifaces.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_uname.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_capability.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_tgkill.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_close.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_pipe.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_sendfile.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/fs/pipe.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/fs/ramfs/ramfs.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_epoll.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_eventfd.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_ppoll.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_read.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_write.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/net/net_layout.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/net/local_socket.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/net/socket.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/net/udp.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/net/raw.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/net/tcp.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/net/ipv4.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/net/icmp.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/net/arp.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/net/net_stack.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/time/timer_table.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_mmap.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_pread.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_sigtimedwait.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_wait.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/wait_time.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_prlimit.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_fcntl.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_futex.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_getdents.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_getrandom.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_openat.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_path.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                           framework/syscall_host.cpp
                           framework/clock_host.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)

add_cinux_test(drivers/keyboard ${CMAKE_SOURCE_DIR}/kernel/drivers/keyboard/scancode.cpp)
add_cinux_test(drivers/input ${CMAKE_SOURCE_DIR}/kernel/drivers/input/event_stream.cpp
                      ${CMAKE_SOURCE_DIR}/kernel/drivers/keyboard/scancode.cpp
                      ${CMAKE_SOURCE_DIR}/kernel/drivers/mouse/mouse_packet.cpp)
target_link_libraries(test_input PRIVATE Threads::Threads)
add_cinux_test(drivers/input_node ${CMAKE_SOURCE_DIR}/kernel/drivers/input/event_stream.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/drivers/device/device.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/fs/devfs/input_node.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/fs/devfs/devfs.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/fs/vfs.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp)
add_cinux_arch_test(ps2_controller ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/platform/ps2/controller.cpp
                             ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp)
add_cinux_test(mm/pmm ${CMAKE_SOURCE_DIR}/kernel/mm/pmm.cpp)
add_cinux_test(mm/heap ${CMAKE_SOURCE_DIR}/kernel/mm/heap.cpp)
add_cinux_test(time/tick)
find_package(Threads REQUIRED)
target_link_libraries(test_tick PRIVATE Threads::Threads)
add_cinux_test(proc/lockdep ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp)
add_cinux_test(base/directed_graph)
add_cinux_test(base/static_stack)
add_cinux_test(base/slot_table)
add_cinux_test(base/ref_count)
target_link_libraries(test_ref_count PRIVATE Threads::Threads)
add_cinux_test(proc/context ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp)
add_cinux_test(proc/fpu_gate ${CMAKE_SOURCE_DIR}/kernel/proc/fpu_gate.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
set_source_files_properties(unit/test_fpu_gate.cpp PROPERTIES COMPILE_OPTIONS "-mno-sse;-mno-sse2")
add_cinux_test(base/self_list)
add_cinux_test(base/shared)
add_cinux_test(loader/elf ${CMAKE_SOURCE_DIR}/kernel/loader/elf_layout.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/loader/entry_picture.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/loader/exec_arguments.cpp)
add_cinux_test(proc/sync ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                      ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                      ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                      ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                      ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                      ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                      ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                      ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
add_cinux_test(base/guard)
add_cinux_test(proc/scheduler ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
target_link_libraries(test_scheduler PRIVATE Threads::Threads)
add_cinux_test(proc/sched_class ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
add_cinux_test(fs/vfs ${CMAKE_SOURCE_DIR}/kernel/fs/vfs.cpp)
add_cinux_test(fs/devfs ${CMAKE_SOURCE_DIR}/kernel/fs/devfs/devfs.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/fs/vfs.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/drivers/device/device.cpp)
add_cinux_test(drivers/device_model ${CMAKE_SOURCE_DIR}/kernel/drivers/device/device.cpp
                             ${CMAKE_SOURCE_DIR}/kernel/fs/devfs/devfs.cpp
                             ${CMAKE_SOURCE_DIR}/kernel/fs/vfs.cpp)
add_cinux_test(net/net_loopback ${CMAKE_SOURCE_DIR}/kernel/drivers/net/loopback.cpp)
add_cinux_test(drivers/tty ${CMAKE_SOURCE_DIR}/kernel/drivers/tty/tty.cpp)
add_cinux_test(fs/pty ${CMAKE_SOURCE_DIR}/kernel/drivers/tty/pty.cpp
                      ${CMAKE_SOURCE_DIR}/kernel/drivers/tty/tty.cpp)
add_cinux_test(fs/pty_node ${CMAKE_SOURCE_DIR}/kernel/drivers/tty/pty.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/drivers/tty/tty.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/drivers/device/device.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/fs/devfs/pty_node.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/fs/devfs/devfs.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/fs/vfs.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_read.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_common.cpp
                          ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_path.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
add_cinux_test(fs/procfs ${CMAKE_SOURCE_DIR}/kernel/fs/procfs/procfs.cpp
                        ${CMAKE_SOURCE_DIR}/kernel/fs/vfs.cpp)
add_cinux_test(fs/file_table ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp)
target_link_libraries(test_file_table PRIVATE Threads::Threads)
add_cinux_test(fs/flock ${CMAKE_SOURCE_DIR}/kernel/fs/file_lock.cpp)
add_cinux_test(fs/ramfs ${CMAKE_SOURCE_DIR}/kernel/fs/ramfs/ramfs.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/fs/vfs.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
add_cinux_test(fs/ustar ${CMAKE_SOURCE_DIR}/kernel/fs/ustar.cpp)
add_cinux_test(fs/ext2 ${CMAKE_SOURCE_DIR}/kernel/fs/ext2/ext2_layout.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/fs/ext2/ext2.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/fs/ext2/ext2_write.cpp
                     ${CMAKE_SOURCE_DIR}/kernel/fs/ext2/ext2_mutation.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/drivers/block/block_cache.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/drivers/block/block_queue.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
add_cinux_arch_test(pci ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/platform/pci/pci_layout.cpp)
add_cinux_arch_test(msi_layout ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/platform/pci/pci_layout.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/platform/msi/msi_layout.cpp
                            ${CMAKE_SOURCE_DIR}/kernel/arch/x86_64/platform/msi/msix.cpp)
add_cinux_test(interrupt/irq ${CMAKE_SOURCE_DIR}/kernel/interrupt/irq.cpp)
add_cinux_test(drivers/xhci_layout ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/xhci_layout.cpp
                           ${CMAKE_SOURCE_DIR}/kernel/drivers/usb/tablet_report.cpp)
add_cinux_test(base/async)
add_cinux_test(drivers/ahci ${CMAKE_SOURCE_DIR}/kernel/drivers/ahci/ahci_identify.cpp
                    ${CMAKE_SOURCE_DIR}/kernel/drivers/ahci/ahci_layout.cpp)
add_cinux_test(drivers/block ${CMAKE_SOURCE_DIR}/kernel/drivers/block/block_queue.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
add_cinux_test(mm/vma ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp)
add_cinux_test(fs/fs_context)
add_cinux_test(proc/signal ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/syscall/sys_signal.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp
                       framework/syscall_host.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                       ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
add_cinux_test(proc/thread_exit ${CMAKE_SOURCE_DIR}/kernel/proc/task.cpp
                             ${CMAKE_SOURCE_DIR}/kernel/proc/scheduler.cpp
                             ${CMAKE_SOURCE_DIR}/kernel/proc/futex.cpp
                             ${CMAKE_SOURCE_DIR}/kernel/mm/vma.cpp
                             ${CMAKE_SOURCE_DIR}/kernel/fs/file_table.cpp
                             ${CMAKE_SOURCE_DIR}/kernel/proc/signals.cpp
                             ${CMAKE_SOURCE_DIR}/kernel/proc/sched_class.cpp
                             ${CMAKE_SOURCE_DIR}/kernel/proc/sync.cpp
                         ${CMAKE_SOURCE_DIR}/test/framework/clock_host.cpp
                             ${CMAKE_SOURCE_DIR}/kernel/proc/lockdep.cpp ${CMAKE_SOURCE_DIR}/kernel/syscall/uaccess.cpp)
add_cinux_test(debug/kallsyms ${CMAKE_SOURCE_DIR}/kernel/debug/kallsyms.cpp)

add_test(NAME arch_boundary
    COMMAND ${CMAKE_COMMAND}
            -DTEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DARCH=${CINUX_ARCHITECTURE}
            -P ${CMAKE_SOURCE_DIR}/cmake/test/test_arch_boundary.cmake)

add_test(NAME kernel_image
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/kernel_image
            -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DOBJCOPY=${CMAKE_OBJCOPY}
            -P ${CMAKE_SOURCE_DIR}/cmake/test/test_kernel_image.cmake)

# Ktest registry entries are "name:stage": the stage names the highest
# ladder level (kernel init order) the test kernel brings up before its
# cases run. Module tests live in test/kernel/, integration tests in
# test/kernel/integration/ — each entry one file, one kernel, one boot.
set(CINUX_KTEST_IMAGE_TARGETS "")
foreach(entry IN LISTS CINUX_KTESTS CINUX_KTESTS_INTEGRATION)
    string(REPLACE ":" ";" entry_parts "${entry}")
    list(GET entry_parts 0 ktest_path)
    list(GET entry_parts 1 ktest_stage)
    get_filename_component(ktest "${ktest_path}" NAME)
    string(REPLACE "/" ";" ktest_path_parts "${ktest_path}")
    list(GET ktest_path_parts 0 ktest_module)
    list(FIND CINUX_KTEST_STAGES "${ktest_stage}" stage_known)
    if(stage_known EQUAL -1)
        message(FATAL_ERROR
            "ktest '${ktest}' declares unknown stage '${ktest_stage}'; known: ${CINUX_KTEST_STAGES}")
    endif()
    string(REPLACE "boot/ext2.img" "boot/ext2_ktest.img" CINUX_KTEST_DEVICE_ARGS
                   "${CINUX_QEMU_DEVICE_ARGS}")
    set(ktest_cpus 1 2)
    if(ktest IN_LIST CINUX_KTESTS_DUAL_CPU)
        set(ktest_cpus 2)
    endif()
    foreach(cpus IN LISTS ktest_cpus)
        set(test_name kernel_test_${ktest}_cpu${cpus})
        add_test(NAME ${test_name}
            COMMAND ${CMAKE_COMMAND}
                    "-DQEMU_PROGRAM=${CINUX_QEMU_COMMAND}"
                    "-DQEMU_DEVICE_ARGS=${CINUX_KTEST_DEVICE_ARGS}"
                    -DCPU_COUNT=${cpus}
                    -DIMAGE_FILE=${CMAKE_BINARY_DIR}/boot/cinux_ktest_${ktest}.img
                    -DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/${test_name}
                    -P ${CMAKE_SOURCE_DIR}/cmake/test/run_kernel_tests.cmake)
        set_tests_properties(${test_name} PROPERTIES
            TIMEOUT 120 LABELS "kernel;capability;${ktest_module};cpu${cpus}"
            RESOURCE_LOCK cinux_qemu)
    endforeach()
    list(APPEND CINUX_KTEST_IMAGE_TARGETS boot_ktest_${ktest})
    add_dependencies(boot_ktest_${ktest} ext2_ktest_img)
endforeach()

set(CINUX_CTEST_JOBS "$ENV{CINUX_CHECK_JOBS}")
if(NOT CINUX_CTEST_JOBS)
    set(CINUX_CTEST_JOBS 2)
endif()

add_custom_target(test_host
    COMMAND ${CMAKE_CTEST_COMMAND} -j ${CINUX_CTEST_JOBS}
            --test-dir ${CMAKE_CURRENT_BINARY_DIR}
            -E ^kernel_test_ --output-on-failure
    DEPENDS ${ALL_HOST_TESTS}
    USES_TERMINAL
    COMMENT "Running host unit tests..."
)

add_custom_target(test_kernel
    COMMAND ${CMAKE_CTEST_COMMAND} -j 1
            --test-dir ${CMAKE_CURRENT_BINARY_DIR}
            -R ^kernel_test_ --output-on-failure
    DEPENDS ${CINUX_KTEST_IMAGE_TARGETS}
    USES_TERMINAL
    COMMENT "Running kernel tests in QEMU..."
)
