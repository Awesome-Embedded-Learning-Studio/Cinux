# Runs the bring-up image for a fixed window and checks the serial banner.
execute_process(
    COMMAND ${QEMU_PROGRAM} -M mcimx6ul-evk -kernel ${IMAGE_FILE}
            -device loader,file=${DEVICE_FILE},addr=${DEVICE_ADDR},force-raw=on
            -nographic -monitor none
    TIMEOUT ${RUN_SECONDS}
    OUTPUT_VARIABLE serial_out
    ERROR_VARIABLE serial_err
    RESULT_VARIABLE run_result)

file(WRITE "${SERIAL_LOG}" "${serial_out}")
if(NOT serial_out MATCHES "${EXPECTED}")
    message(FATAL_ERROR
        "arm_boot: serial banner missing (result ${run_result}).\n"
        "stdout:\n${serial_out}\nstderr:\n${serial_err}")
endif()
if(EXPECTED_ROTATE AND NOT serial_out MATCHES "${EXPECTED_ROTATE}")
    message(FATAL_ERROR
        "arm_boot: the two user tasks never finished rotating.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_TASK_DONE AND NOT serial_out MATCHES "${EXPECTED_TASK_DONE}")
    message(FATAL_ERROR "arm_boot: task 1 never reported done.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_SVC AND NOT serial_out MATCHES "${EXPECTED_SVC}")
    message(FATAL_ERROR
        "arm_boot: the svc write path failed.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_USER AND NOT serial_out MATCHES "${EXPECTED_USER}")
    message(FATAL_ERROR
        "arm_boot: the user-mode round trip failed.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_TASKS AND NOT serial_out MATCHES "${EXPECTED_TASKS}")
    message(FATAL_ERROR
        "arm_boot: the two-task round robin failed.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_PERCPU AND NOT serial_out MATCHES "${EXPECTED_PERCPU}")
    message(FATAL_ERROR
        "arm_boot: the per-CPU block check failed.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_CONTEXT AND NOT serial_out MATCHES "${EXPECTED_CONTEXT}")
    message(FATAL_ERROR
        "arm_boot: the context switch round trip failed.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_MAP AND NOT serial_out MATCHES "${EXPECTED_MAP}")
    message(FATAL_ERROR
        "arm_boot: the mapped-page check never passed.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_FAULT AND NOT serial_out MATCHES "${EXPECTED_FAULT}")
    message(FATAL_ERROR
        "arm_boot: the deliberate fault was not reported.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_PAGED AND NOT serial_out MATCHES "${EXPECTED_PAGED}")
    message(FATAL_ERROR
        "arm_boot: the paged heap never came up.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_HEAP AND NOT serial_out MATCHES "${EXPECTED_HEAP}")
    message(FATAL_ERROR
        "arm_boot: the heap self-check did not pass.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_CONSOLE AND NOT serial_out MATCHES "${EXPECTED_CONSOLE}")
    message(FATAL_ERROR
        "arm_boot: the console path never printed.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_SELFCHECK AND NOT serial_out MATCHES "${EXPECTED_SELFCHECK}")
    message(FATAL_ERROR
        "arm_boot: the in-image self-check did not pass.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_PMM AND NOT serial_out MATCHES "${EXPECTED_PMM}")
    message(FATAL_ERROR
        "arm_boot: the physical page manager never reported.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_MEMORY AND NOT serial_out MATCHES "${EXPECTED_MEMORY}")
    message(FATAL_ERROR
        "arm_boot: the device tree memory range never reached the log.\nstdout:\n${serial_out}")
endif()
if(EXPECTED_TICK AND NOT serial_out MATCHES "${EXPECTED_TICK}")
    message(FATAL_ERROR
        "arm_boot: no timer tick reached the serial log.\nstdout:\n${serial_out}")
endif()
message(STATUS "arm_boot: banner seen")
