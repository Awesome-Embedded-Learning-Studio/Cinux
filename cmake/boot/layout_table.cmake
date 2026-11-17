# Assembles a disk image by executing a one-image-per-file layout table
# from scripts/meta/. The table file holds every offset decision for its
# image; this function only names the table and feeds it payload sources.
# The script validates the table (bounds, capacity, overlaps past the
# image end) and fails the build with the offending entry named, so a bad
# layout cannot produce a half image.

function(add_cinux_layout_image name image_file layout_table)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPENDS" ${ARGN})
    set(_table ${CMAKE_SOURCE_DIR}/scripts/meta/${layout_table})
    set(_command ${CINUX_PYTHON_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/scripts/assemble_image.py
                 --layout ${_table}
                 --output ${image_file})
    foreach(_pair IN LISTS ARG_SOURCES)
        list(APPEND _command --source ${_pair})
    endforeach()
    add_custom_target(${name}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/boot
        COMMAND ${_command}
        DEPENDS ${ARG_DEPENDS}
                ${CMAKE_SOURCE_DIR}/scripts/assemble_image.py
                ${_table}
        VERBATIM
        USES_TERMINAL)
endfunction()
