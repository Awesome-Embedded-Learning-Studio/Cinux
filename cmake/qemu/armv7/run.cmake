# QEMU side of the armv7 lane: the interactive run target and the boot check.

find_program(CINUX_ARM_QEMU qemu-system-arm)
if(NOT CINUX_ARM_QEMU)
    message(FATAL_ERROR "armv7 lane: qemu-system-arm not found in PATH")
endif()

set(CINUX_ARM_RUN_SECONDS 5 CACHE STRING "How long the arm_boot check lets QEMU run")

set(CINUX_ARM_DTB_ADDR 0x81000000)

add_custom_target(run
    COMMAND ${CINUX_ARM_QEMU} -M mcimx6ul-evk -kernel $<TARGET_FILE:arm_image>
            -device loader,file=${CINUX_ARM_DTB},addr=${CINUX_ARM_DTB_ADDR},force-raw=on
            -nographic -monitor none
    DEPENDS arm_image arm_dtb
    USES_TERMINAL
    COMMENT "Booting the armv7 bring-up image")

add_test(NAME arm_boot
    COMMAND ${CMAKE_COMMAND}
            -DQEMU_PROGRAM=${CINUX_ARM_QEMU}
            -DIMAGE_FILE=$<TARGET_FILE:arm_image>
            -DRUN_SECONDS=${CINUX_ARM_RUN_SECONDS}
            -DDEVICE_FILE=${CINUX_ARM_DTB}
            -DDEVICE_ADDR=${CINUX_ARM_DTB_ADDR}
            -DEXPECTED=Cinux armv7: alive on i.MX6ULL
            -DEXPECTED_TICK=tick 1
            -DEXPECTED_MEMORY=dtb memory 0x80000000
            -DEXPECTED_PMM=pmm free pages
            -DEXPECTED_SELFCHECK=selfcheck pmm ok
            -DEXPECTED_CONSOLE=console up
            -DEXPECTED_HEAP=selfcheck heap ok
            -DEXPECTED_PAGED=paged heap live
            -DEXPECTED_MAP=mapped page ok
            -DEXPECTED_CONTEXT=context switch ok
            -DEXPECTED_PERCPU=per-cpu ok
            -DEXPECTED_TASKS=two tasks ok
            -DEXPECTED_USER=user mode round trip ok
            -DEXPECTED_FAULT=data abort at 51000000 status 805 code 2
            -DSERIAL_LOG=${CMAKE_BINARY_DIR}/serial-test.log
            -P ${CMAKE_SOURCE_DIR}/cmake/qemu/armv7/boot_check.cmake)
