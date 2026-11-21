if(NOT DEFINED DESK_BIN)
    message(FATAL_ERROR "check_desk.cmake: DESK_BIN not given")
endif()
if(NOT EXISTS "${DESK_BIN}")
    message(FATAL_ERROR
        "run needs the vesper desk (${DESK_BIN}), which does not exist.\n"
        "Build it first: scripts/create/build-armv7-vesper-desk.sh "
        "(needs a GCC 14 armhf cross toolchain), then re-run run.")
endif()
