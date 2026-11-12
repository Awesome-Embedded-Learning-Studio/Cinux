# QEMU side of the armv7 lane: the interactive run target and the boot check.

find_program(CINUX_ARM_QEMU qemu-system-arm)
if(NOT CINUX_ARM_QEMU)
    message(FATAL_ERROR "armv7 lane: qemu-system-arm not found in PATH")
endif()

set(CINUX_ARM_RUN_SECONDS 5 CACHE STRING "How long the arm_boot check lets QEMU run")

add_custom_target(run
    COMMAND ${CINUX_ARM_QEMU} -M mcimx6ul-evk -kernel $<TARGET_FILE:arm_image>
            -nographic -monitor none
    DEPENDS arm_image
    USES_TERMINAL
    COMMENT "Booting the armv7 bring-up image")

add_test(NAME arm_boot
    COMMAND ${CMAKE_COMMAND}
            -DQEMU_PROGRAM=${CINUX_ARM_QEMU}
            -DIMAGE_FILE=$<TARGET_FILE:arm_image>
            -DRUN_SECONDS=${CINUX_ARM_RUN_SECONDS}
            -DEXPECTED=Cinux armv7: alive on i.MX6ULL
            -DEXPECTED_TICK=tick 1
            -DSERIAL_LOG=${CMAKE_BINARY_DIR}/serial-test.log
            -P ${CMAKE_SOURCE_DIR}/cmake/qemu/armv7/boot_check.cmake)
