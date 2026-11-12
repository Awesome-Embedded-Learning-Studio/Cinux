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
