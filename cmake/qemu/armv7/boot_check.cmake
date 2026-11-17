# The USB device list is a knob: the station lanes keep the default
# keyboard; desktop lanes put keyboard and tablet on separate buses.
math(EXPR _grace "${RUN_SECONDS} + 20")
if(NOT DEFINED USB_DEVICES)
    set(USB_DEVICES "-device usb-kbd")
endif()
# A -D value with spaces arrives as a semicolon list; the shell branch
# wants the plain string back, and the direct branch wants real argv
# words, so keep both forms.
string(REPLACE ";" " " USB_DEVICES "${USB_DEVICES}")
separate_arguments(USB_DEVICE_ARGS UNIX_COMMAND "${USB_DEVICES}")

# Runs the bring-up image for a fixed window and checks the serial banner.
# With STDIN_FILE set, the file's bytes drip into the guest's UART after
# a delay: bytes that arrive before the kernel's UART bring-up are
# dropped by the model, and the legacy task world's pending reads would
# swallow them right after, so the wait skips both windows.
if(STDIN_FILE)
    execute_process(
        COMMAND /bin/sh -c "{ sleep ${STDIN_DELAY}; head -n 1 '${STDIN_FILE}'; sleep ${STDIN_DELAY2}; sed -n 2p '${STDIN_FILE}'; sleep ${STDIN_DELAY3}; tail -n +3 '${STDIN_FILE}'; } | 'timeout' '${RUN_SECONDS}' '${QEMU_PROGRAM}' -M mcimx6ul-evk -kernel '${IMAGE_FILE}' -device loader,file='${DEVICE_FILE}',addr=${DEVICE_ADDR},force-raw=on -sd '${SD_FILE}' -nographic -monitor none ${USB_DEVICES}"
        TIMEOUT ${_grace}
        OUTPUT_VARIABLE serial_out
        ERROR_VARIABLE serial_err
        RESULT_VARIABLE run_result)
else()
    execute_process(
        COMMAND ${QEMU_PROGRAM} -M mcimx6ul-evk -kernel ${IMAGE_FILE}
                -device loader,file=${DEVICE_FILE},addr=${DEVICE_ADDR},force-raw=on
                -sd ${SD_FILE}
                -nographic -monitor none ${USB_DEVICE_ARGS}
        TIMEOUT ${RUN_SECONDS}
        OUTPUT_VARIABLE serial_out
        ERROR_VARIABLE serial_err
        RESULT_VARIABLE run_result)
endif()
file(WRITE "${SERIAL_LOG}" "${serial_out}")

if(NOT EXISTS ${SERIAL_LOG})
    message(FATAL_ERROR
        "arm_boot: the runner produced no log (result ${run_result}).")
endif()
file(READ ${SERIAL_LOG} serial_out)
if(serial_out STREQUAL "")
    message(FATAL_ERROR "arm_boot: the runner produced an empty log (err: ${serial_err}).")
endif()

file(WRITE "${SERIAL_LOG}" "${serial_out}")

# Expectations are literal text, not regular expressions: a checked line
# may contain '+', '(', or any other metacharacter without changing what
# the check means.
function(expect_literal variable message_text)
    if(NOT ${variable})
        return()
    endif()
    string(FIND "${serial_out}" "${${variable}}" found_at)
    if(found_at EQUAL -1)
        # Keep the scene: the next run overwrites SERIAL_LOG.
        string(TIMESTAMP stamp "%Y%m%d-%H%M%S")
        set(kept "${SERIAL_LOG}.fail-${stamp}.log")
        file(WRITE "${kept}" "${serial_out}")
        message(FATAL_ERROR
            "arm_boot: ${message_text} (expected \"${${variable}}\")\n"
            "kept: ${kept}\nstdout:\n${serial_out}\nstderr:\n${serial_err}")
    endif()
endfunction()

expect_literal(EXPECTED_ROTATE "the scheduler two-task rotation did not answer.")

expect_literal(EXPECTED_USER "the user-mode round trip failed.")

expect_literal(EXPECTED_TASKS "the two-task round robin failed.")

expect_literal(EXPECTED_FORKCLONE "the user subtree clone failed.")

expect_literal(EXPECTED_SHARED "the shared frame did not survive.")

expect_literal(EXPECTED_COW "the share ledger never balanced.")

expect_literal(EXPECTED_TEARDOWN "the space teardown never balanced.")

expect_literal(EXPECTED_PERCPU "the per-CPU block check failed.")

expect_literal(EXPECTED_CPUID "the MIDR identity probe did not answer.")

expect_literal(EXPECTED_PROCCPU "the /proc/cpuinfo read-back probe did not answer.")

expect_literal(EXPECTED_SCHED "the shared-scheduler round trip did not answer.")

expect_literal(EXPECTED_LCDIF "the LCDIF bring-up line did not answer.")

expect_literal(EXPECTED_USDHC "the usdhc street never read the card: the block lane stayed dark.")

expect_literal(EXPECTED_USDHC_WRITE "the usdhc street could not write and read a sector back.")

expect_literal(EXPECTED_MMCBLK0 "the mmcblk0 seat never answered through the block port.")

expect_literal(EXPECTED_MMCBLK1 "the partition table never told its first entry.")

expect_literal(EXPECTED_EXT2 "the ext2 volume never mounted or its motd never answered.")

expect_literal(EXPECTED_EHCI "the ehci host never reported its seated keyboard.")

expect_literal(EXPECTED_EHCI_SEATED "the ehci host seated no keyboard: the control street never delivered the device descriptor.")

expect_literal(EXPECTED_EHCI_KEYBOARD "the ehci keyboard never walked the neutral street: the periodic schedule seated no report door.")

expect_literal(EXPECTED_USBKBD "the usb keyboard never landed its usbkbd node in the devfs tree.")

expect_literal(EXPECTED_FB0 "the /dev/fb0 seat did not answer.")

expect_literal(EXPECTED_EVENT0 "the /dev/event0 seat did not answer.")

expect_literal(EXPECTED_SEATWRITE "the seated task's console write did not answer.")

expect_literal(EXPECTED_SEATOK "the scheduler-seat user round trip did not finish.")

expect_literal(EXPECTED_CONTEXT "the context switch round trip failed.")

expect_literal(EXPECTED_MAP "the mapped-page check never passed.")

expect_literal(EXPECTED_FAULT "the deliberate fault was not reported.")

expect_literal(EXPECTED_PAGED "the paged heap never came up.")

expect_literal(EXPECTED_HEAP "the heap self-check did not pass.")

expect_literal(EXPECTED_CONSOLE "the console path never printed.")

expect_literal(EXPECTED_SELFCHECK "the in-image self-check did not pass.")

expect_literal(EXPECTED_PMM "the physical page manager never reported.")

expect_literal(EXPECTED_MEMORY "the device tree memory range never reached the log.")

expect_literal(EXPECTED_FIRMWARE "the dtb firmware face did not answer.")

expect_literal(EXPECTED_TICK "no timer tick reached the serial log.")

expect_literal(EXPECTED_TAKEOVER "the kernel init lane never took over.")

expect_literal(EXPECTED_IRQS "the init lane never raised its IRQ door.")

expect_literal(EXPECTED_PING "the kernel init ping never answered.")

expect_literal(EXPECTED_SYSINIT "the card's init never printed Cinux up.")

expect_literal(EXPECTED_SHELL_ECHO "the console shell never echoed the typed line.")

expect_literal(EXPECTED_ADOPTED "the card's file world never answered through sh.")

message(STATUS "arm_boot: banner seen")
