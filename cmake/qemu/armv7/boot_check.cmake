# Runs the bring-up image for a fixed window and checks the serial banner.
# With STDIN_FILE set, the file's bytes drip into the guest's UART after
# a delay: bytes that arrive before the kernel's UART bring-up are
# dropped by the model, and the legacy task world's pending reads would
# swallow them right after, so the wait skips both windows.
if(STDIN_FILE)
    execute_process(
        COMMAND /bin/sh -c "(sleep ${STDIN_DELAY} && cat '${STDIN_FILE}') | '${QEMU_PROGRAM}' -M mcimx6ul-evk -kernel '${IMAGE_FILE}' -device loader,file='${DEVICE_FILE}',addr=${DEVICE_ADDR},force-raw=on -nographic -monitor none"
        TIMEOUT ${RUN_SECONDS}
        OUTPUT_VARIABLE serial_out
        ERROR_VARIABLE serial_err
        RESULT_VARIABLE run_result)
else()
    execute_process(
        COMMAND ${QEMU_PROGRAM} -M mcimx6ul-evk -kernel ${IMAGE_FILE}
                -device loader,file=${DEVICE_FILE},addr=${DEVICE_ADDR},force-raw=on
                -nographic -monitor none
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
    message(FATAL_ERROR "arm_boot: the runner produced an empty log.")
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

expect_literal(EXPECTED_ROTATE "the two user tasks never finished rotating.")

expect_literal(EXPECTED_READ "the read path never answered.")

expect_literal(EXPECTED_TASK_DONE "task 0 never reported done.")

expect_literal(EXPECTED_REFUSED "a kernel pointer was accepted as a user buffer.")

expect_literal(EXPECTED_PREEMPT "no preemption was observed.")

expect_literal(EXPECTED_FAULTED "task 1 never reported faulted.")

expect_literal(EXPECTED_USERFAULT "the cross-table fault was not caught.")

expect_literal(EXPECTED_SVC "the svc write path failed.")

expect_literal(EXPECTED_USER "the user-mode round trip failed.")

expect_literal(EXPECTED_TASKS "the two-task round robin failed.")

expect_literal(EXPECTED_BLOCK2 "the second waiter never blocked.")

expect_literal(EXPECTED_CLOSE "nothing was ever closed.")

expect_literal(EXPECTED_EBADF "a closed descriptor was still writable.")
expect_literal(EXPECTED_FDRANGE "an out-of-table descriptor was accepted.")
expect_literal(EXPECTED_OPEN "the built-in file never opened.")
expect_literal(EXPECTED_FILEREAD "no bytes came out of the file.")
expect_literal(EXPECTED_FILEOK "the file contents were wrong.")
expect_literal(EXPECTED_ENOENT "a missing name was accepted.")
expect_literal(EXPECTED_LSEEK "lseek never moved the offset.")
expect_literal(EXPECTED_LSEEKOK "the offset did not survive a rewind.")
expect_literal(EXPECTED_MOTD "the second built-in file never opened.")
expect_literal(EXPECTED_MOTDOK "the second file's contents were wrong.")
expect_literal(EXPECTED_SCRATCH "the writable file never opened.")
expect_literal(EXPECTED_WRITEFILE "the written bytes did not come back.")
expect_literal(EXPECTED_NOSLOTOPEN "the full table did not say so.")
expect_literal(EXPECTED_APPEND "appending at the end did not work.")
expect_literal(EXPECTED_FSTAT "fstat did not report the file length.")
expect_literal(EXPECTED_FSTATRO "the read-only file reported other numbers.")
expect_literal(EXPECTED_FSTATROOK "the caller read the wrong stat words.")
expect_literal(EXPECTED_FSTATBAD "a kernel address was accepted for stat.")
expect_literal(EXPECTED_FSTATFD "a bad descriptor was accepted for stat.")
expect_literal(EXPECTED_FSTATOK "the caller read the wrong stat words.")
expect_literal(EXPECTED_SHAREDFILE "another task saw a different file.")
expect_literal(EXPECTED_LSEEKBAD "a negative offset was accepted.")
expect_literal(EXPECTED_READBAD "a kernel buffer was accepted for reading.")
expect_literal(EXPECTED_ROWRITE "a read-only file accepted a write.")
expect_literal(EXPECTED_ROWRITEOK "the write refusal never reached the caller.")
expect_literal(EXPECTED_REOPEN "closing did not free the slot.")
expect_literal(EXPECTED_README "the third built-in file never opened.")
expect_literal(EXPECTED_READMEOK "the third file's contents were wrong.")
expect_literal(EXPECTED_LSEEKBADOK "the rejected seek never reached the caller.")
expect_literal(EXPECTED_READBADOK "the rejected read never reached the caller.")
expect_literal(EXPECTED_MULTIOPEN "two opens shared one offset.")
expect_literal(EXPECTED_FULL "the descriptor table never filled up.")

expect_literal(EXPECTED_MMAP "the anonymous mapping never appeared.")
expect_literal(EXPECTED_HINTTOOK "a taken hint was accepted.")
expect_literal(EXPECTED_HINTOK "the hinted mapping did not work.")
expect_literal(EXPECTED_MUNMAP "munmap did not release the page.")
expect_literal(EXPECTED_ROCOPY "the read-only page was never copied.")
expect_literal(EXPECTED_ROOK "the read-only page never became writable.")
expect_literal(EXPECTED_BADFLAGS "an unknown mmap flag was accepted.")
expect_literal(EXPECTED_FLAGSOK "the refused flag was not reported.")
expect_literal(EXPECTED_YIELD "sched_yield never answered.")
expect_literal(EXPECTED_HANDLED "the registered fault handler never ran.")
expect_literal(EXPECTED_HANDLEROK "the fault handler never reported.")
expect_literal(EXPECTED_HANDLEREXIT "the handler's exit status was lost.")
expect_literal(EXPECTED_STATUS2 "wait did not report the exited task's status.")
expect_literal(EXPECTED_STATUSOK "the statuses the user read back were wrong.")
expect_literal(EXPECTED_FORK "fork never produced a child.")
expect_literal(EXPECTED_CHILD "the first child never reported.")
expect_literal(EXPECTED_CHILDTWO "the second child never reported.")
expect_literal(EXPECTED_FORK5 "the second fork never produced a child.")
expect_literal(EXPECTED_CHILDEXIT5 "the second child's exit status was lost.")
expect_literal(EXPECTED_FORKTWOS "the parent saw a child's value.")
expect_literal(EXPECTED_HANDLFAULT "the inherited handler never ran.")
expect_literal(EXPECTED_HANDLERINH "the child's handler exit was lost.")
expect_literal(EXPECTED_GETPPID "getppid never answered.")
expect_literal(EXPECTED_PPIDOK "a child saw the wrong parent.")
expect_literal(EXPECTED_ORPHAN "an orphan saw a stale parent.")
expect_literal(EXPECTED_GETTICKS "getticks never answered.")
expect_literal(EXPECTED_TICKSOK "the clock did not move across a sleep.")
expect_literal(EXPECTED_GRANDCHILD "the grandchild never reported.")
expect_literal(EXPECTED_TWOLEVELS "the second level's reaping failed.")
expect_literal(EXPECTED_GRANDEXIT "the grandchild's exit status was lost.")
expect_literal(EXPECTED_FDINH "the closed descriptor came back in the child.")
expect_literal(EXPECTED_BRKINH "the child's heap did not start where the parent's was.")
expect_literal(EXPECTED_NOSLOT "a ninth task was created.")
expect_literal(EXPECTED_NOSLOTOK "the refusal never reached the caller.")
expect_literal(EXPECTED_TASK6 "the parked third child never exited.")
expect_literal(EXPECTED_TASK7 "the parked fourth child never exited.")
expect_literal(EXPECTED_CHILDEXIT "the child's exit status was lost.")
expect_literal(EXPECTED_FORKCOW "the child's write was not copied.")
expect_literal(EXPECTED_FORKCOWOK "the parent saw the child's value.")

expect_literal(EXPECTED_MMAPOK "the mapped pages were not writable.")

expect_literal(EXPECTED_BRK "brk never grew the heap.")

expect_literal(EXPECTED_BRKOK "the grown heap was not writable.")

expect_literal(EXPECTED_UNKNOWN "an unknown call was not refused.")

expect_literal(EXPECTED_PID "a task did not learn its own id.")

expect_literal(EXPECTED_SLEEP "no task ever went to sleep.")

expect_literal(EXPECTED_WOKE "the sleeping task never woke.")

expect_literal(EXPECTED_TIMEOUT "no wait ever timed out.")

expect_literal(EXPECTED_WAKE "the precise wake never happened.")

expect_literal(EXPECTED_BLOCK "the waiter never blocked.")

expect_literal(EXPECTED_WAIT "the waiting task never reaped a sibling.")

expect_literal(EXPECTED_THIRD "the third task never ran.")

expect_literal(EXPECTED_KEEPS "a task lost its private page.")

expect_literal(EXPECTED_COWCOPY "the shared page was never copied.")

expect_literal(EXPECTED_REPAIR "the demand page was never repaired.")

expect_literal(EXPECTED_FORKCLONE "the user subtree clone failed.")

expect_literal(EXPECTED_SHARED "the shared frame did not survive.")

expect_literal(EXPECTED_COW "the share ledger never balanced.")

expect_literal(EXPECTED_TEARDOWN "the space teardown never balanced.")

expect_literal(EXPECTED_PERCPU "the per-CPU block check failed.")
expect_literal(EXPECTED_CPUID "the MIDR identity probe did not answer.")
expect_literal(EXPECTED_PROCCPU "the /proc/cpuinfo read-back probe did not answer.")
expect_literal(EXPECTED_SCHED "the shared-scheduler round trip did not answer.")
expect_literal(EXPECTED_LCDIF "the LCDIF bring-up line did not answer.")
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

expect_literal(EXPECTED_TICK "no timer tick reached the serial log.")

message(STATUS "arm_boot: banner seen")
