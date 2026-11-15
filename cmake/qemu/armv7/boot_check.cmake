# The USB device list is a knob: the station lanes keep the default
# keyboard, the desktop lanes trade it for the tablet on the board's
# single port.
math(EXPR _grace "${RUN_SECONDS} + 20")
if(NOT DEFINED USB_DEVICES)
    set(USB_DEVICES "-device usb-kbd")
endif()
# A -D value with spaces arrives as a semicolon list; the shell below
# wants the plain string back.
string(REPLACE ";" " " USB_DEVICES "${USB_DEVICES}")

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
                -nographic -monitor none ${USB_DEVICES}
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

expect_literal(EXPECTED "serial banner missing")

expect_literal(EXPECTED_ROTATE "the scheduler two-task rotation did not answer.")






expect_literal(EXPECTED_USERFAULT "the cross-table fault was not caught.")


expect_literal(EXPECTED_USER "the user-mode round trip failed.")

expect_literal(EXPECTED_TASKS "the two-task round robin failed.")



expect_literal(EXPECTED_FSTATOK "the caller read the wrong stat words.")


expect_literal(EXPECTED_MMAPOK "the mapped pages were not writable.")


expect_literal(EXPECTED_BRKOK "the grown heap was not writable.")













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
expect_literal(EXPECTED_GUEST_DISK_MOTD "the guest never read the ext2 motd through its own syscalls.")
expect_literal(EXPECTED_GUEST_NAP "the guest's nanosleep never waited: the clock did not advance.")
expect_literal(EXPECTED_GUEST_POLL "the guest's poll timeout never waited: the clock did not advance.")
expect_literal(EXPECTED_GUEST_LINK "the card's /bin/sh link never resolved: the root layout is missing.")
expect_literal(EXPECTED_DISK_EXEC "the exec never brought the disk program into the seat.")
if(DEFINED EXPECTED_GLIBC_EXEC AND NOT EXPECTED_GLIBC_EXEC STREQUAL "")
    expect_literal(EXPECTED_GLIBC_EXEC "the glibc station never printed: a real libc did not run.")
    expect_literal(EXPECTED_GLIBC_LIBC "the libc surface probe never answered: malloc, printf or open failed.")
    expect_literal(EXPECTED_GLIBC_FB "the guest never read the panel's marker: the frame-buffer mapping stayed private.")
    expect_literal(EXPECTED_GLIBC_STDIO "the glibc station's stdio read did not answer.")
endif()
if(DEFINED EXPECTED_ARM_BUSYBOX AND NOT EXPECTED_ARM_BUSYBOX STREQUAL "")
    expect_literal(EXPECTED_ARM_BUSYBOX "busybox never answered: a real userland program did not run.")
endif()
if(DEFINED EXPECTED_ARM_SHELL AND NOT EXPECTED_ARM_SHELL STREQUAL "")
    expect_literal(EXPECTED_ARM_SHELL "the card's shell never ran its command line.")
endif()
if(DEFINED EXPECTED_ARM_INTERACTIVE AND NOT EXPECTED_ARM_INTERACTIVE STREQUAL "")
    expect_literal(EXPECTED_ARM_INTERACTIVE "the bare interactive shell never ran its typed command.")
endif()
if(DEFINED EXPECTED_ARM_SHELL_TYPED AND NOT EXPECTED_ARM_SHELL_TYPED STREQUAL "")
    expect_literal(EXPECTED_ARM_SHELL_TYPED "the shell never read the console's typed line.")
endif()
if(DEFINED EXPECTED_ARM_SHELL_CAT AND NOT EXPECTED_ARM_SHELL_CAT STREQUAL "")
    expect_literal(EXPECTED_ARM_SHELL_CAT "the shell's cat never answered through sendfile.")
endif()
expect_literal(EXPECTED_EHCI "the ehci host never reported its seated keyboard.")
expect_literal(EXPECTED_EHCI_SEATED "the ehci host seated no keyboard: the control street never delivered the device descriptor.")
expect_literal(EXPECTED_EHCI_KEYBOARD "the ehci keyboard never walked the neutral street: the periodic schedule seated no report door.")
expect_literal(EXPECTED_USBKBD "the usb keyboard never landed its usbkbd node in the devfs tree.")
expect_literal(EXPECTED_GUEST_USBKBD "the guest never read the usbkbd face: the glyph path stayed dark.")
expect_literal(EXPECTED_FB0 "the /dev/fb0 seat did not answer.")
expect_literal(EXPECTED_EVENT0 "the /dev/event0 seat did not answer.")
expect_literal(EXPECTED_GUESTLOAD "the neutral loader did not seat the guest image.")
expect_literal(EXPECTED_GUESTRUN "the guest did not ride the real scheduler.")
expect_literal(EXPECTED_GUESTEXIT "the guest process did not exit cleanly.")
expect_literal(EXPECTED_GUESTMOTD "the guest did not read the motd through the neutral read path.")
expect_literal(EXPECTED_GUESTFORK "the seated fork/wait/cow duel did not answer.")
expect_literal(EXPECTED_GUESTLITTER "the multi-process litter probe did not answer.")
expect_literal(EXPECTED_GUESTEDGES "the syscall edge probe did not answer.")
expect_literal(EXPECTED_GUESTGRIM "the faulted-child reaper probe did not answer.")
expect_literal(EXPECTED_GUESTSLICE "the preemption slice probe did not answer.")
expect_literal(EXPECTED_SEATCOW "the seat cow duel did not answer.")
expect_literal(EXPECTED_SEATROCOPY "the read-only repair did not answer.")
expect_literal(EXPECTED_GUESTHELLO "the loaded guest did not speak from user mode.")
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

message(STATUS "arm_boot: banner seen")
