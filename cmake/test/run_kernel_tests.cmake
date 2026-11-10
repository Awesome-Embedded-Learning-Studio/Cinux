# Runs the kernel-world test image under QEMU and asserts the exit code.
# The kernel leaves via isa-debug-exit with code = failures + 1, so QEMU
# exits with 2*(failures + 1) + 1: 3 means every case passed, any other
# value names the failure shape. The serial log is the autopsy report.

file(MAKE_DIRECTORY "${TEST_DIR}")

# Configure already picked an accelerator from CINUX_QEMU_ACCEL and baked it
# into QEMU_DEVICE_ARGS. A build configured on a KVM host can still run where
# /dev/kvm is absent or unreadable (containers, WSL2 without nested
# virtualisation), so rewrite the pair to TCG instead of refusing to boot.
# CI keeps /dev/kvm, so the arguments stay untouched there.
list(FIND QEMU_DEVICE_ARGS -accel accel_index)
set(qemu_accel "")
if(NOT accel_index EQUAL -1)
    math(EXPR accel_value_index "${accel_index} + 1")
    list(GET QEMU_DEVICE_ARGS ${accel_value_index} qemu_accel)
endif()

if(qemu_accel STREQUAL "kvm")
    execute_process(COMMAND test -c /dev/kvm -a -w /dev/kvm RESULT_VARIABLE kvm_usable)
    if(NOT kvm_usable EQUAL 0)
        message(WARNING
            "/dev/kvm is not usable here: falling back to tcg for this kernel test "
            "(emulated, slow)")
        list(REMOVE_AT QEMU_DEVICE_ARGS ${accel_value_index})
        list(INSERT QEMU_DEVICE_ARGS ${accel_value_index} tcg)
        set(qemu_accel tcg)
        list(FIND QEMU_DEVICE_ARGS -cpu cpu_index)
        if(NOT cpu_index EQUAL -1)
            math(EXPR cpu_value_index "${cpu_index} + 1")
            list(REMOVE_AT QEMU_DEVICE_ARGS ${cpu_value_index})
            list(INSERT QEMU_DEVICE_ARGS ${cpu_value_index} max)
        endif()
    endif()
endif()

# TCG emulates every instruction, so give it more headroom while staying
# below CTest's 120 s limit in test/CMakeLists.txt.
set(qemu_timeout 60)
if(qemu_accel STREQUAL "tcg")
    set(qemu_timeout 100)
endif()

set(serial_log "${TEST_DIR}/serial.log")
file(REMOVE "${serial_log}")

if(NOT CPU_COUNT MATCHES "^[12]$")
    message(FATAL_ERROR "Kernel tests require an explicit CPU_COUNT of 1 or 2")
endif()
list(FIND QEMU_DEVICE_ARGS -smp smp_index)
if(smp_index EQUAL -1)
    message(FATAL_ERROR "QEMU_DEVICE_ARGS lacks -smp")
endif()
math(EXPR smp_count_index "${smp_index} + 1")
list(REMOVE_AT QEMU_DEVICE_ARGS ${smp_count_index})
list(INSERT QEMU_DEVICE_ARGS ${smp_count_index} ${CPU_COUNT})

execute_process(
    COMMAND ${QEMU_PROGRAM} ${QEMU_DEVICE_ARGS}
            -snapshot
            -drive format=raw,file=${IMAGE_FILE}
            -serial file:${serial_log}
    RESULT_VARIABLE qemu_status
    TIMEOUT ${qemu_timeout}
    OUTPUT_QUIET ERROR_VARIABLE qemu_stderr)

if(NOT qemu_status EQUAL 3 AND NOT qemu_stderr STREQUAL "")
    message(WARNING "QEMU diagnostics: ${qemu_stderr}")
endif()

function(dump_serial_and_fail reason)
    if(EXISTS "${serial_log}")
        file(READ "${serial_log}" serial_output)
        message(FATAL_ERROR "kernel tests failed: ${reason}\n--- serial log ---\n${serial_output}")
    endif()
    message(FATAL_ERROR "kernel tests failed: ${reason} (no serial log at ${serial_log})")
endfunction()

if(qemu_status EQUAL 3)
    if(NOT EXISTS "${serial_log}")
        dump_serial_and_fail("success exit without a serial log")
    endif()
    file(READ "${serial_log}" serial_output)
    if(NOT serial_output MATCHES "=== Results: ([1-9][0-9]*) passed, 0 failed ===")
        dump_serial_and_fail("success exit without a nonempty passing test summary")
    endif()
    message(STATUS "kernel tests: all passed (serial log: ${serial_log})")
elseif(qemu_status MATCHES "^[0-9]+$" AND qemu_status GREATER_EQUAL 3)
    math(EXPR failed_cases "(${qemu_status} - 3) / 2")
    dump_serial_and_fail("QEMU exit ${qemu_status}, ${failed_cases} failing case(s)")
elseif(qemu_status EQUAL 1)
    dump_serial_and_fail("QEMU exit 1: the kernel never reached the exit port (early death)")
else()
    dump_serial_and_fail("QEMU ended with '${qemu_status}' (hang or crash)")
endif()
