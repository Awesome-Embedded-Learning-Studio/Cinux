# QEMU side of the armv7 lane: the interactive run target and the boot check.

find_program(CINUX_ARM_QEMU qemu-system-arm)
if(NOT CINUX_ARM_QEMU)
    message(FATAL_ERROR "armv7 lane: qemu-system-arm not found in PATH")
endif()

set(CINUX_ARM_RUN_SECONDS 5 CACHE STRING "How long the arm_boot check lets QEMU run")

set(CINUX_ARM_DTB_ADDR 0x81000000)

add_custom_target(run
    COMMAND ${CINUX_ARM_QEMU} -M mcimx6ul-evk -kernel $<TARGET_FILE:arm_image>
            -device loader,file=${CINUX_ARM_DTB},addr=${CINUX_ARM_DTB_ADDR},force-raw=on
            -nographic -monitor none
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
            "-DEXPECTED_FB0=dev fb0 320x240"
            "-DEXPECTED_EVENT0=dev event0 72 bytes"
            "-DEXPECTED_PTY=pty ok, master and slave echo"
            "-DEXPECTED_GUESTLOAD=load guest entry 40000000 phnum 2"
            "-DEXPECTED_GUESTRUN=guest runs on the scheduler entry 40000000"
            "-DEXPECTED_GUESTEXIT=guest exited with status 0"
            "-DEXPECTED_GUESTMOTD=[guest] motd: "
            "-DEXPECTED_GUESTHELLO=[guest] hello from the loaded image"
            "-DEXPECTED_GUESTBRK=[guest] brk ok, heap page reads back"
            "-DEXPECTED_GUESTMMAP=[guest] mmap ok, anon page reads back"
            "-DEXPECTED_SEATWRITE=svc write 40 bytes"
            "-DEXPECTED_SEATOK=sched user seat ok"
            "-DEXPECTED_TEARDOWN=space teardown ok"
            "-DEXPECTED_COW=cow ledger ok"
            "-DEXPECTED_SHARED=shared frame ok"
            "-DEXPECTED_FORKCLONE=fork clone ok"
            "-DEXPECTED_REPAIR=fault repaired at 40009000"
            "-DEXPECTED_COWCOPY=cow copy at 40008000"
            "-DEXPECTED_KEEPS=task 1 keeps its page"
            "-DEXPECTED_THIRD=task 2 keeps its page"
            "-DEXPECTED_WAIT=wait 1 -> DEAD"
            "-DEXPECTED_BLOCK=task 0 blocks on 1"
            "-DEXPECTED_BLOCK2=task 3 blocks on 4"
            "-DEXPECTED_WAKE=wake a waiter of 4"
            "-DEXPECTED_TIMEOUT=wait timed out"
            "-DEXPECTED_SLEEP=task 0 sleeps 3 ticks"
            "-DEXPECTED_PID=pid 3 ok"
            "-DEXPECTED_UNKNOWN=svc FFFF unknown"
            "-DEXPECTED_BRK=brk -> 4000E000"
            "-DEXPECTED_BRKOK=brk ok"
            "-DEXPECTED_MMAP=mmap 40010000 len 2000"
            "-DEXPECTED_MMAPOK=mmap ok"
            "-DEXPECTED_HINTTOOK=mmap hint 40020000 is taken"
            "-DEXPECTED_HINTOK=mmap hint ok"
            "-DEXPECTED_MUNMAP=munmap 40020000 pages 1"
            "-DEXPECTED_ROCOPY=ro copy at 40030000"
            "-DEXPECTED_ROOK=ro copy ok"
            "-DEXPECTED_BADFLAGS=mmap flags 2 refused"
            "-DEXPECTED_FLAGSOK=flags refused"
            "-DEXPECTED_YIELD=yield ok"
            "-DEXPECTED_HANDLED=fault handled at 51000000"
            "-DEXPECTED_HANDLEROK=handler ok"
            "-DEXPECTED_HANDLEREXIT=task 2 exited with status 9"
            "-DEXPECTED_STATUS2=wait 2 -> 9"
            "-DEXPECTED_STATUSOK=status ok"
            "-DEXPECTED_FORK=fork 0 -> 4"
            "-DEXPECTED_CHILD=child one ok"
            "-DEXPECTED_CHILDTWO=child two ok"
            "-DEXPECTED_CHILDEXIT=task 4 exited with status 3"
            "-DEXPECTED_CHILDEXIT5=task 5 exited with status 3"
            "-DEXPECTED_FORK5=fork 0 -> 5"
            "-DEXPECTED_FORKTWOS=cow after two forks"
            "-DEXPECTED_HANDLFAULT=fault handled at 51000010"
            "-DEXPECTED_HANDLERINH=handler inherited ok"
            "-DEXPECTED_GETPPID=getppid 0"
            "-DEXPECTED_PPIDOK=ppid ok"
            "-DEXPECTED_ORPHAN=orphan ok"
            "-DEXPECTED_GETTICKS=getticks"
            "-DEXPECTED_TICKSOK=ticks ok"
            "-DEXPECTED_GRANDCHILD=grandchild ok"
            "-DEXPECTED_TWOLEVELS=two levels ok"
            "-DEXPECTED_GRANDEXIT=task 6 exited with status 4"
            "-DEXPECTED_FDINH=fd inherited ok"
            "-DEXPECTED_BRKINH=brk inherited ok"
            "-DEXPECTED_NOSLOT=fork refused, no free slot"
            "-DEXPECTED_NOSLOTOK=no slot ok"
            "-DEXPECTED_TASK6=task 6 exited with status 3"
            "-DEXPECTED_TASK7=task 7 exited with status 3"
            "-DEXPECTED_FORKCOW=cow copy at 40040000"
            "-DEXPECTED_FORKCOWOK=cow after two forks"
            "-DEXPECTED_EBADF=ebadf ok"
            "-DEXPECTED_FDRANGE=fd range ok"
            "-DEXPECTED_OPEN=open hello as 3"
            "-DEXPECTED_FILEREAD=file read 6"
            "-DEXPECTED_FILEOK=file ok"
            "-DEXPECTED_ENOENT=enoent ok"
            "-DEXPECTED_LSEEK=lseek 3 -> 0"
            "-DEXPECTED_LSEEKOK=lseek ok"
            "-DEXPECTED_MOTD=open motd as"
            "-DEXPECTED_MOTDOK=motd ok"
            "-DEXPECTED_SCRATCH=open scratch as"
            "-DEXPECTED_WRITEFILE=writefile ok"
            "-DEXPECTED_NOSLOTOPEN=open refused, no free slot"
            "-DEXPECTED_APPEND=append ok"
            "-DEXPECTED_FSTAT=size 8 writable 1"
            "-DEXPECTED_FSTATRO=size 6 writable 0"
            "-DEXPECTED_FSTATROOK=fstat ro ok"
            "-DEXPECTED_FSTATBAD=fstat bad ok"
            "-DEXPECTED_FSTATFD=fd stat bad"
            "-DEXPECTED_FSTATOK=fstat ok"
            "-DEXPECTED_SHAREDFILE=shared ok"
            "-DEXPECTED_LSEEKBAD=refused, whence 0"
            "-DEXPECTED_READBAD=read refused 80000000"
            "-DEXPECTED_ROWRITE=refused, read-only"
            "-DEXPECTED_ROWRITEOK=ro write ok"
            "-DEXPECTED_REOPEN=reopen ok"
            "-DEXPECTED_README=open readme as"
            "-DEXPECTED_READMEOK=readme ok"
            "-DEXPECTED_LSEEKBADOK=lseek bad ok"
            "-DEXPECTED_READBADOK=read bad ok"
            "-DEXPECTED_MULTIOPEN=multi open ok"
            "-DEXPECTED_FULL=full ok"
            "-DEXPECTED_CLOSE=close 2"
            "-DEXPECTED_WOKE=task 0 woke from sleep"
            "-DEXPECTED_TASKS=two tasks ok"
            "-DEXPECTED_USER=user mode round trip ok"
            "-DEXPECTED_SVC=svc write"
            "-DEXPECTED_ROTATE=all user tasks done"
            "-DEXPECTED_TASK_DONE=task 0 exited with status 7"
            "-DEXPECTED_READ=read timed out"
            "-DEXPECTED_FAULTED=task 1 faulted"
            "-DEXPECTED_PREEMPT=preempt to task"
            "-DEXPECTED_REFUSED=svc write refused -14 at 80000000"
            "-DEXPECTED_USERFAULT=user fault caught at 4000B000 code 4"
            "-DEXPECTED_FAULT=data abort at 51000000 status 805 code 2"
            "-DSERIAL_LOG=${CMAKE_BINARY_DIR}/serial-test.log"
            -P ${CMAKE_SOURCE_DIR}/cmake/qemu/armv7/boot_check.cmake)
