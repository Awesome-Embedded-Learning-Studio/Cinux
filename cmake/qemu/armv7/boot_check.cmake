# The USB device list is a knob: the station lanes keep the default
# keyboard; desktop lanes put keyboard and tablet on separate buses.
if(NOT DEFINED USB_DEVICES)
    set(USB_DEVICES "-device usb-kbd")
endif()
# A -D value with spaces arrives as a semicolon list; the shell branch
# wants the plain string back, so rebuild one string.
string(REPLACE ";" " " USB_DEVICES "${USB_DEVICES}")

# Event-driven runner: QEMU starts detached with its serial line teed
# into SERIAL_LOG, and this script polls the log instead of sleeping out
# a fixed window. The run ends the moment every EXPECTED_* literal this
# lane passed has landed (success), or at the grace deadline (failure --
# the expect_literal tail below then names the missing marker).
# With STDIN_FILE set, a feeder shell holds the probe lines back until
# the guest prints STDIN_AFTER (default: the sysinit banner), then
# streams them with short gaps -- the bytes used to race the kernel's
# UART bring-up when they arrived early, so they wait for the guest's
# own word. -snapshot keeps concurrent boots off each other's card: the
# selftest lane writes sectors into the shared image, and parallel ctest
# runs would both lock and corrupt it.
math(EXPR _grace "${RUN_SECONDS} + 20")
if(STDIN_FILE)
    if(NOT DEFINED STDIN_AFTER)
        set(STDIN_AFTER "Cinux up")
    endif()
    set(_feeder
        "until grep -aq -- '${STDIN_AFTER}' '${SERIAL_LOG}' 2>/dev/null; do sleep 0.2; done; sleep 1; while IFS= read -r _line; do printf '%s\\n' \"\$_line\"; sleep 0.4; done < '${STDIN_FILE}'")
else()
    set(_feeder "sleep ${_grace}")
endif()
set(_err_log "${SERIAL_LOG}.qemu.err")
set(_pid_file "${SERIAL_LOG}.qemu.pid")
get_filename_component(_tmp_dir "${SERIAL_LOG}" DIRECTORY)
execute_process(COMMAND /bin/sh -c
    "export TMPDIR='${_tmp_dir}'; { { ${_feeder}; } | '${QEMU_PROGRAM}' -M mcimx6ul-evk -kernel '${IMAGE_FILE}' -device loader,file='${DEVICE_FILE}',addr=${DEVICE_ADDR},force-raw=on -sd '${SD_FILE}' -snapshot -nographic -monitor none ${USB_DEVICES} > '${SERIAL_LOG}' 2> '${_err_log}'; } > /dev/null 2>&1 < /dev/null & echo \$! > '${_pid_file}'"
    OUTPUT_QUIET ERROR_QUIET)

# The verdict is every expected literal present in the log.
get_cmake_property(_all_vars VARIABLES)
set(_expected_names "")
foreach(_name IN LISTS _all_vars)
    if(_name MATCHES "^EXPECTED_" AND NOT "${${_name}}" STREQUAL "")
        list(APPEND _expected_names "${_name}")
    endif()
endforeach()
string(TIMESTAMP _deadline "%s")
math(EXPR _deadline "${_deadline} + ${_grace}")
set(_complete FALSE)
while(NOT _complete)
    if(EXISTS "${SERIAL_LOG}")
        file(READ "${SERIAL_LOG}" serial_out)
        set(_missing FALSE)
        foreach(_name IN LISTS _expected_names)
            string(FIND "${serial_out}" "${${_name}}" _at)
            if(_at EQUAL -1)
                set(_missing TRUE)
            endif()
        endforeach()
        if(NOT _missing)
            set(_complete TRUE)
        endif()
    endif()
    if(NOT _complete)
        string(TIMESTAMP _now "%s")
        if(_now GREATER_EQUAL _deadline)
            # Let the guest drain what it was printing before judging.
            execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 1)
            set(_complete TRUE)
        else()
            execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.2)
        endif()
    endif()
endwhile()
if(EXISTS "${_pid_file}")
    file(READ "${_pid_file}" _qemu_pid)
    string(STRIP "${_qemu_pid}" _qemu_pid)
    execute_process(COMMAND /bin/sh -c "kill '${_qemu_pid}' 2>/dev/null || true")
endif()

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
