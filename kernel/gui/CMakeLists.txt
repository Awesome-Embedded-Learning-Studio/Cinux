#
# kernel/gui/CMakeLists.txt
#
# @brief GUI subsystem CMake configuration
#
# Contains GUI-only components: event queue, window manager, etc.
#

# Event queue (ring buffer for mouse/keyboard events)
target_sources(big_kernel_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/event.cpp
)

# Window class (off-screen canvas, title bar, hit testing)
target_sources(big_kernel_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/window.cpp
)

# Window manager (Z-ordering, compositing, drag)
target_sources(big_kernel_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/window_manager.cpp
)

# GUI initialisation (mouse driver, WM setup, PIT tick callback)
target_sources(big_kernel_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gui_init.cpp
)

# Terminal window (80x25 character buffer, cursor, ANSI sequences)
target_sources(big_kernel_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/terminal.cpp
)

# visor Host ABI skeleton (F13 §3a): ABI headers + compile-time self-check.
# visor core does not take over yet; this only pins the ABI boundary
# (machine-verifies header sizes / packed-ness across kernel/user/MCU builds).
target_sources(big_kernel_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/visor_core/visor_abi_check.cpp
)
