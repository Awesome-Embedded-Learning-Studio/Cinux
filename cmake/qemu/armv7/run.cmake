# QEMU side of the armv7 lane: the interactive run target and the boot check.

find_program(CINUX_ARM_QEMU qemu-system-arm)
if(NOT CINUX_ARM_QEMU)
    message(FATAL_ERROR "armv7 lane: qemu-system-arm not found in PATH")
endif()

set(CINUX_ARM_RUN_SECONDS 30 CACHE STRING "How long the arm_boot check lets QEMU run")

set(CINUX_ARM_DTB_ADDR 0x81000000)

# The bring-up SD card: an eight-megabyte raw image whose MBR carries
# the boot signature, one Linux partition starting at sector 2048, and
# a marker string in the second sector. Deterministic bytes, so the
# block street's reads have something to recognize.
set(CINUX_ARM_SD_IMG ${CMAKE_BINARY_DIR}/armv7-sd.img)
add_custom_command(
    OUTPUT ${CINUX_ARM_SD_IMG}
    COMMAND /bin/sh ${CMAKE_SOURCE_DIR}/scripts/create_armv7_sd.sh ${CINUX_ARM_SD_IMG}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/create_armv7_sd.sh
            ${CMAKE_SOURCE_DIR}/scripts/ext2_seed/motd.txt
            ${CMAKE_SOURCE_DIR}/assets/armv7_diskprog.elf
    VERBATIM
    COMMENT "Molding the armv7 SD bring-up image")

# The glibc station's program is optional: only a host with the cross
# toolchain builds it, and the image remolds when it appears.
if(EXISTS ${CMAKE_SOURCE_DIR}/assets/armv7_glibc_hello.elf)
    set_property(SOURCE ${CINUX_ARM_SD_IMG} APPEND PROPERTY
        OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/assets/armv7_glibc_hello.elf)
endif()
if(_arm_busybox_bin)
    set_property(SOURCE ${CINUX_ARM_SD_IMG} APPEND PROPERTY
        OBJECT_DEPENDS "${_arm_busybox_bin}")
endif()
if(_arm_desk_bin)
    set_property(SOURCE ${CINUX_ARM_SD_IMG} APPEND PROPERTY
        OBJECT_DEPENDS "${_arm_desk_bin}")
endif()

add_custom_target(arm_sd_image DEPENDS ${CINUX_ARM_SD_IMG})

add_custom_target(run
    COMMAND ${CINUX_ARM_QEMU} -M mcimx6ul-evk -kernel $<TARGET_FILE:arm_image>
            -device loader,file=${CINUX_ARM_DTB},addr=${CINUX_ARM_DTB_ADDR},force-raw=on
            -sd ${CINUX_ARM_SD_IMG}
            -nographic -monitor none -device usb-kbd
    DEPENDS arm_image arm_dtb arm_sd_image
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

# The desktop station rides the same rule: built by
# scripts/build-armv7-vesper-desk.sh on a host with the GCC 14 armhf tree.
set(_arm_desk_bin "")
foreach(_arm_candidate "${CMAKE_SOURCE_DIR}/build/armv7-vesper/desk"
        "${CMAKE_BINARY_DIR}/armv7-vesper/desk")
    if(EXISTS "${_arm_candidate}" AND NOT _arm_desk_bin)
        set(_arm_desk_bin "${_arm_candidate}")
    endif()
endforeach()

# The busybox station rides the same optional-artifact rule: built by
# scripts/build-armv7-busybox.sh on a host with the cross toolchain.
set(_arm_busybox_arg "")
set(_arm_busybox_bin "")
foreach(_arm_candidate "${CMAKE_SOURCE_DIR}/build/armv7-busybox/busybox"
        "${CMAKE_BINARY_DIR}/armv7-busybox/busybox")
    if(EXISTS "${_arm_candidate}" AND NOT _arm_busybox_bin)
        set(_arm_busybox_bin "${_arm_candidate}")
    endif()
endforeach()
if(_arm_busybox_bin)
    # busybox runs one applet: `stat` proves the binary started, the
    # metadata street answered, and the record reached its own libc.
    set(_arm_busybox_arg "-DEXPECTED_ARM_BUSYBOX=File: /disk/prog")
endif()

# The glibc station rides an optional artifact: only a host with the
# cross toolchain built it, so its judgement joins only there.
set(_arm_glibc_arg "")
if(EXISTS ${CMAKE_SOURCE_DIR}/assets/armv7_glibc_hello.elf)
    set(_arm_glibc_arg "-DEXPECTED_GLIBC_EXEC=[glibc] hello from ext2, real libc ran")
    list(APPEND _arm_glibc_arg
         "-DEXPECTED_GLIBC_LIBC=[glibc] malloc, printf, open all ok")
    # The frame-buffer landing: the kernel stamps a marker before the
    # program starts, the program reads it through its own mapping.
    list(APPEND _arm_glibc_arg "-DEXPECTED_GLIBC_FB=[glibc] fb seed ABCD1234")
endif()

add_test(NAME arm_boot
    COMMAND ${CMAKE_COMMAND}
            "-DQEMU_PROGRAM=${CINUX_ARM_QEMU}"
            "-DIMAGE_FILE=$<TARGET_FILE:arm_image>"
            "-DRUN_SECONDS=${CINUX_ARM_RUN_SECONDS}"
            "-DDEVICE_FILE=${CINUX_ARM_DTB}"
            "-DDEVICE_ADDR=${CINUX_ARM_DTB_ADDR}"
            "-DSD_FILE=${CINUX_ARM_SD_IMG}"
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
            "-DEXPECTED_LCDIF=lcdif 640x480 fb"
            "-DEXPECTED_USDHC=usdhc card 16384 sectors mbr 55AA"
            "-DEXPECTED_USDHC_WRITE=usdhc write readback ok"
            "-DEXPECTED_MMCBLK0=dev mmcblk0 16384 sectors mbr 55AA"
            "-DEXPECTED_MMCBLK1=mmcblk0 p1 at 2048"
            "-DEXPECTED_EXT2=ext2 mounted, motd is 99 bytes"
            "-DEXPECTED_EHCI=ehci 2184200 rev 100 ports 1, port 1 attached"
            "-DEXPECTED_EHCI_SEATED=ehci device 0627:0001 seated"
    "-DEXPECTED_EHCI_KEYBOARD=ehci keyboard street ok"
            "-DEXPECTED_USBKBD=dev usbkbd seated"
            "-DEXPECTED_GUEST_USBKBD=usbkbd ok, empty answers EAGAIN or a glyph"
            "-DEXPECTED_GUEST_DISK_MOTD=[guest] disk motd: Cinux says hello from a real disk."
            "-DEXPECTED_DISK_EXEC=[disk] hello from ext2, exec replaced me"
            ${_arm_glibc_arg}
            ${_arm_busybox_arg}

            "-DEXPECTED_FB0=dev fb0 640x480"
            "-DEXPECTED_EVENT0=dev event0 72 bytes"
            "-DEXPECTED_PTY=pty ok, master and slave echo"
            "-DEXPECTED_FCNTL=fcntl ok, empty stdin answers EAGAIN"
            "-DEXPECTED_GUESTSTAT=fstat ok, motd is a regular 9-byte file"
            "-DEXPECTED_GETDENTS=getdents ok, root holds six entries with hello"
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
