# QEMU side of the armv7 lane: the interactive run target and the boot check.

find_program(CINUX_ARM_QEMU qemu-system-arm)
if(NOT CINUX_ARM_QEMU)
    message(FATAL_ERROR "armv7 lane: qemu-system-arm not found in PATH")
endif()

set(CINUX_ARM_RUN_SECONDS 55 CACHE STRING "How long the arm_boot check lets QEMU run")

set(CINUX_ARM_DTB_ADDR 0x81000000)

# The bring-up SD card: an eight-megabyte raw image whose MBR carries
# the boot signature, one Linux partition starting at sector 2048, and
# a marker string in the second sector. Deterministic bytes, so the
# The buildroot volume's path, spelled here: the lane's cmake loads
# before the root's add_subdirectory(buildroot), so the variable that
# the buildroot side exports does not exist yet at this point. The path
# is the same word the buildroot directory computes.
set(CINUX_ARM_BUILDROOT_VOLUME ${CMAKE_BINARY_DIR}/boot/native-build.ext2)

# The desk rides the build graph: the overlay stage copies it onto the
# buildroot card, so the volume target declares the dependency and a
# plain cmake --build produces everything.
add_custom_target(vesper_desk
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/create/build-armv7-vesper-desk.sh
            ${CMAKE_SOURCE_DIR}/build/armv7-vesper/desk
    COMMENT "armv7 vesper desk (cached by the script's input fingerprint)")
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
            "-DIMAGE_FILE=$<TARGET_FILE:arm_selftest>"
            "-DRUN_SECONDS=${CINUX_ARM_RUN_SECONDS}"
            "-DDEVICE_FILE=${CINUX_ARM_DTB}"
            "-DDEVICE_ADDR=${CINUX_ARM_DTB_ADDR}"
            "-DSD_FILE=${CINUX_ARM_BUILDROOT_VOLUME}"
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
            "-DEXPECTED_USDHC=usdhc card 4194304 sectors mbr 55AA"
            "-DEXPECTED_USDHC_WRITE=usdhc write readback ok"
            "-DEXPECTED_MMCBLK0=dev mmcblk0 4194304 sectors mbr 55AA"
            "-DEXPECTED_MMCBLK1=mmcblk0 p1 at 2048"
            "-DEXPECTED_EXT2=ext2 mounted, motd is 99 bytes"
            "-DEXPECTED_EHCI=ehci 2184200 rev 100 ports 1, port 1 attached"
            "-DEXPECTED_EHCI_SEATED=ehci device 0627:0001 seated"
    "-DEXPECTED_EHCI_KEYBOARD=ehci keyboard street ok"
            "-DEXPECTED_USBKBD=dev usbkbd seated"
            "-DEXPECTED_FB0=dev fb0 640x480"
            "-DEXPECTED_EVENT0=dev event0 48 bytes"
            "-DEXPECTED_SEATWRITE=[seat] scheduler task writes from user m"
            "-DEXPECTED_SEATOK=sched user seat ok"
            "-DEXPECTED_TEARDOWN=space teardown ok"
            "-DEXPECTED_COW=cow ledger ok"
            "-DEXPECTED_SHARED=shared frame ok"
            "-DEXPECTED_FORKCLONE=fork clone ok"
            "-DEXPECTED_TASKS=two tasks ok"
            "-DEXPECTED_USER=user mode round trip ok"
            "-DEXPECTED_ROTATE=two tasks ok"
            "-DEXPECTED_FAULT=data abort at 51000000 status 805 code 2"
            "-DSERIAL_LOG=${CMAKE_BINARY_DIR}/serial-test.log"
            -P ${CMAKE_SOURCE_DIR}/cmake/qemu/armv7/boot_check.cmake)

# The full-boot lane: the same image behind the init dtb, so kernel_init
# -- not the station chain -- owns the boot. One typed probe asks the
# card's shell to speak, and the judgement listens for its answer. The
# runner lives in its own script so the -D names here stay single.
add_test(NAME arm_init
    COMMAND ${CMAKE_COMMAND} -E env
            "CINUX_ARM_BUILD=${CMAKE_BINARY_DIR}"
            "CINUX_ARM_RUN_SECONDS=${CINUX_ARM_RUN_SECONDS}"
            bash "${CMAKE_SOURCE_DIR}/scripts/checks/check-armv7-init.sh")
set_tests_properties(arm_init PROPERTIES TIMEOUT 300)

# The GUI lane is a test too: it boots the desktop, clicks the terminal
# icon, types, and judges the screen's pixels. The desk never exits, so
# the script owns its own run. The desk script builds the desk artifact
# itself, so there is no configure-time existence guard: the test_arm
# target below carries every input the suite boots.
    add_test(NAME arm_desk
        COMMAND ${CMAKE_COMMAND} -E env
                "CINUX_ARM_BUILD=${CMAKE_BINARY_DIR}"
                bash "${CMAKE_SOURCE_DIR}/scripts/checks/check-armv7-desk.sh")
    set_tests_properties(arm_desk PROPERTIES TIMEOUT 900)
    add_test(NAME arm_gui_keyboard
        COMMAND python3 "${CMAKE_SOURCE_DIR}/test/test_armv7_gui_keyboard.py"
                --build "${CMAKE_BINARY_DIR}" --qemu "${CINUX_ARM_QEMU}")
    set_tests_properties(arm_gui_keyboard PROPERTIES TIMEOUT 90)

# One target that carries every input the ctest suite boots: the
# selftest kernel (arm_boot), the real image with all three dtb doors
# plus the buildroot card (arm_init, arm_gui_keyboard), and the desk
# (arm_desk). `cmake --build <build> --target test_arm && ctest` works
# cold; no test demands a manually prebuilt artifact.
add_custom_target(test_arm DEPENDS arm_selftest arm_image buildroot_volume vesper_desk)

# The interactive door the x86 lane has always had: `run` boots the
# real system, not the self-check world. kernel_init takes over, pid 1
# climbs the volume's /sbin/init, and the console shell answers on the
# serial terminal -- type at it once "Cinux up" and the respawn lines
# have passed. The root is the buildroot card, the same root the x86
# lane's run mounts; CINUX_ARM_ROOTFS can name another card (the boot
# check's fixture card, for example) without editing this file. The
# board's LCD opens as a QEMU window and the GUI dtb says
# CINUX_SESSION=gui so the shared desktop script lights the desk
# beside the console shell: a windowed desk is the one posture, there
# is no console-only lane. Keyboard and tablet use separate chipidea
# buses, feeding /dev/event0 and /dev/event1.
set(CINUX_ARM_ROOTFS "${CINUX_ARM_BUILDROOT_VOLUME}"
    CACHE FILEPATH "rootfs card the armv7 run targets mount")
add_custom_target(run
    COMMAND ${CINUX_ARM_QEMU} -M mcimx6ul-evk -kernel $<TARGET_FILE:arm_image>
            -device loader,file=${CINUX_ARM_INIT_GUI_DTB},addr=${CINUX_ARM_DTB_ADDR},force-raw=on
            -drive if=sd,format=raw,file=${CINUX_ARM_ROOTFS}
            -display gtk,gl=off -serial stdio -monitor none
            -device usb-kbd,bus=usb-bus.0 -device usb-tablet,bus=usb-bus.1
    DEPENDS arm_image arm_init_gui_dtb
    USES_TERMINAL
    COMMENT "Booting full Cinux on armv7 (GUI desk, console shell on this terminal)")
if(CINUX_ARM_ROOTFS STREQUAL CINUX_ARM_BUILDROOT_VOLUME)
    add_dependencies(run buildroot_volume)
endif()
