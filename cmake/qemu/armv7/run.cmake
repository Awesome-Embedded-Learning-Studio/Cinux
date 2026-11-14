# QEMU side of the armv7 lane: the interactive run target and the boot check.

find_program(CINUX_ARM_QEMU qemu-system-arm)
if(NOT CINUX_ARM_QEMU)
    message(FATAL_ERROR "armv7 lane: qemu-system-arm not found in PATH")
endif()

set(CINUX_ARM_RUN_SECONDS 30 CACHE STRING "How long the arm_boot check lets QEMU run")

set(CINUX_ARM_DTB_ADDR 0x81000000)

add_custom_target(run
    COMMAND ${CINUX_ARM_QEMU} -M mcimx6ul-evk -kernel $<TARGET_FILE:arm_image>
            -device loader,file=${CINUX_ARM_DTB},addr=${CINUX_ARM_DTB_ADDR},force-raw=on
            -nographic -monitor none -device usb-kbd
    DEPENDS arm_image arm_dtb
    USES_TERMINAL
    COMMENT "Booting the armv7 bring-up image")

# Every -D value that contains a space must be quoted: an unquoted one is
# split into several arguments and the check only ever sees its first word.
# A repeated -D name silently keeps only the last value, which quietly
# drops an assertion. Refuse that at configure time.
file(READ ${CMAKE_CURRENT_LIST_FILE} _arm_self)
string(REGEX MATCHALL "-D([A-Za-z0-9_]+)=" _arm_defs "${_arm_self}")
set(_arm_names "")
foreach(_arm_def IN LISTS _arm_defs)
    string(REGEX REPLACE "^-D([A-Za-z0-9_]+)=.*$" "\\1" _arm_name "${_arm_def}")
    list(FIND _arm_names "${_arm_name}" _arm_seen)
    if(_arm_seen GREATER -1)
        message(FATAL_ERROR
            "arm_boot: -D${_arm_name} appears twice; only the last would survive")
    endif()
    list(APPEND _arm_names "${_arm_name}")
endforeach()

add_test(NAME arm_boot
    COMMAND ${CMAKE_COMMAND}
            "-DQEMU_PROGRAM=${CINUX_ARM_QEMU}"
            "-DIMAGE_FILE=$<TARGET_FILE:arm_image>"
            "-DRUN_SECONDS=${CINUX_ARM_RUN_SECONDS}"
            "-DDEVICE_FILE=${CINUX_ARM_DTB}"
            "-DDEVICE_ADDR=${CINUX_ARM_DTB_ADDR}"
            "-DEXPECTED=Cinux armv7: alive on i.MX6ULL"
            "-DEXPECTED_TICK=tick 1"
            "-DEXPECTED_MEMORY=dtb memory 0x80000000"
            "-DEXPECTED_FIRMWARE=firmware init-env console=ttymxc0 seat=dtb"
            "-DEXPECTED_PMM=pmm free pages"
            "-DEXPECTED_SELFCHECK=selfcheck pmm ok"
            "-DEXPECTED_CONSOLE=console up"
            "-DEXPECTED_HEAP=selfcheck heap ok"
            "-DEXPECTED_PAGED=paged heap live"
            "-DEXPECTED_MAP=mapped page ok"
            "-DEXPECTED_CONTEXT=context switch ok"
            "-DEXPECTED_PERCPU=per-cpu ok"
            "-DEXPECTED_CPUID=cpu id ARM C07"
            "-DEXPECTED_PROCCPU=proc cpuinfo"
            "-DEXPECTED_SCHED=sched roundtrip ok"
            "-DEXPECTED_LCDIF=lcdif 320x240 fb"
            "-DEXPECTED_EHCI=ehci 2184200 rev 100 ports 1, port 1 attached"
            "-DEXPECTED_EHCI_SEATED=ehci device 0627:0001 seated"
    "-DEXPECTED_EHCI_KEYBOARD=ehci keyboard street ok"
            "-DEXPECTED_FB0=dev fb0 320x240"
            "-DEXPECTED_EVENT0=dev event0 72 bytes"
            "-DEXPECTED_PTY=pty ok, master and slave echo"
            "-DEXPECTED_FCNTL=fcntl ok, empty stdin answers EAGAIN"
            "-DEXPECTED_GUESTSTAT=fstat ok, motd is a regular 9-byte file"
            "-DEXPECTED_GETDENTS=getdents ok, root holds five entries with hello"
            "-DEXPECTED_STDIN=stdin ok, line read back"
            "-DSTDIN_FILE=${CMAKE_SOURCE_DIR}/assets/armv7_stdin.txt"
            "-DSTDIN_DELAY=6"
            "-DEXPECTED_GUESTLOAD=load guest entry 40000000 phnum 2"
            "-DEXPECTED_GUESTRUN=guest runs on the scheduler entry 40000000"
            "-DEXPECTED_GUESTEXIT=guest exited with status 0"
            "-DEXPECTED_GUESTMOTD=[guest] motd: "
            "-DEXPECTED_GUESTHELLO=[guest] hello from the loaded image"
            "-DEXPECTED_GUESTBRK=[guest] brk ok, heap page reads back"
            "-DEXPECTED_GUESTMMAP=[guest] mmap ok, anon page reads back"
            "-DEXPECTED_GUESTFORK=[guest] fork ok, child reaped with own page"
            "-DEXPECTED_GUESTLITTER=[guest] litter ok, three young and a grandchild"
            "-DEXPECTED_GUESTEDGES=[guest] edges ok, refusals refused and hints honored"
            "-DEXPECTED_GUESTGRIM=[guest] grim reaper ok, faulted child answered DEAD"
            "-DEXPECTED_GUESTSLICE=[guest] slice ok, two spinners shared one core"
            "-DEXPECTED_SEATCOW=code write repaired"
            "-DEXPECTED_SEATROCOPY=ro copy at 40010000"
            "-DEXPECTED_SEATWRITE=[seat] scheduler task writes from user m"
            "-DEXPECTED_SEATOK=sched user seat ok"
            "-DEXPECTED_TEARDOWN=space teardown ok"
            "-DEXPECTED_COW=cow ledger ok"
            "-DEXPECTED_SHARED=shared frame ok"
            "-DEXPECTED_FORKCLONE=fork clone ok"
            "-DEXPECTED_BRKOK=brk ok"
            "-DEXPECTED_MMAPOK=mmap ok"
            "-DEXPECTED_FSTATOK=fstat ok"
            "-DEXPECTED_TASKS=two tasks ok"
            "-DEXPECTED_USER=user mode round trip ok"
            "-DEXPECTED_ROTATE=two tasks ok"
            "-DEXPECTED_USERFAULT=user fault caught at 50000000"
            "-DEXPECTED_FAULT=data abort at 51000000 status 805 code 2"
            "-DSERIAL_LOG=${CMAKE_BINARY_DIR}/serial-test.log"
            -P ${CMAKE_SOURCE_DIR}/cmake/qemu/armv7/boot_check.cmake)
