# Assembles a disk image by executing a one-image-per-file layout table
# from scripts/create/meta/. The table file holds every offset decision
# and the image's own name for its image; this function only names the
# table and feeds it payload sources. The script validates the table
# (bounds, capacity, overlaps past the image end) and fails the build
# with the offending entry named, so a bad layout cannot produce a half
# image.
#
# The output path comes from the table's "image" field under
# ${CMAKE_BINARY_DIR}/boot/ unless OUTPUT overrides it -- the ktest
# images share the boot table but seat each kernel under its own name.

function(add_cinux_layout_image name layout_table)
    cmake_parse_arguments(ARG "" "OUTPUT" "SOURCES;DEPENDS" ${ARGN})
    set(_table ${CMAKE_SOURCE_DIR}/scripts/create/meta/${layout_table})
    if(NOT ARG_OUTPUT)
        file(READ ${_table} _table_text)
        string(JSON _image GET ${_table_text} "image")
        set(ARG_OUTPUT ${CMAKE_BINARY_DIR}/boot/${_image})
    endif()
    set(_command ${CINUX_PYTHON_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/scripts/create/assemble_image.py
                 --layout ${_table}
                 --output ${ARG_OUTPUT})
    foreach(_pair IN LISTS ARG_SOURCES)
        list(APPEND _command --source ${_pair})
    endforeach()
    add_custom_target(${name}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/boot
        COMMAND ${_command}
        DEPENDS ${ARG_DEPENDS}
                ${CMAKE_SOURCE_DIR}/scripts/create/assemble_image.py
                ${_table}
        VERBATIM
        USES_TERMINAL)
endfunction()
