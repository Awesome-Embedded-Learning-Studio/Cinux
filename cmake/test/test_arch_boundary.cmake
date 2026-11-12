# Fails when a capability test reaches into the architecture's own headers.
# Capability tests prove what the OS does and must speak the neutral surface;
# architecture tests live under test/arch/<isa>/ and are exempt by location.
file(GLOB_RECURSE CAPABILITY_TESTS "${TEST_DIR}/unit/*.cpp")
set(VIOLATIONS "")
foreach(test_file IN LISTS CAPABILITY_TESTS)
    file(READ "${test_file}" test_source)
    if(test_source MATCHES "kernel/arch/${ARCH}/")
        list(APPEND VIOLATIONS "${test_file}")
    endif()
endforeach()
if(VIOLATIONS)
    message(FATAL_ERROR "capability tests reach into arch/${ARCH}: ${VIOLATIONS}")
endif()
message(STATUS "arch boundary clean: ${ARCH}")
