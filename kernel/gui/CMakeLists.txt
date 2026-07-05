#
# kernel/gui/CMakeLists.txt
#
# @brief GUI subsystem CMake configuration (F13-B)
#
# F13-B (2026-07-05): the old in-tree desktop layer (window / window_manager /
# terminal / desktop_icon / icon -- ~1500 lines) is removed -- the new Cinux-GUI
# core absorbed all of it into host-neutral widget/ (WindowManager + Window +
# Label + ... + Compositor + Desktop). kernel/gui/ now keeps ONLY:
#   - event.cpp        : unified mouse+keyboard EventQueue (host_cinux poll_event
#                        drains it; test_mouse_event exercises it)
#   - host_cinux.cpp   : the Cinux host adapter -- the single host-specific TU
#                        that fills the Host ABI table against the kernel's
#                        drivers (the ONLY hard seam, per DIRECTIVES D)
#   - gui_init.cpp     : PS/2 mouse + keyboard listener registration
#   - desktop_launch.cpp: GUI-side launch_userspace + handoff_framebuffer_to_gui
#                        (§14 single interface, non-GUI counterpart in proc/)
# The host-neutral core (widget tree + Compositor + GuiCore + swraster + region)
# lives at third_party/Cinux-GUI/ and is built as the cinux-gui STATIC lib
# (linked from kernel/CMakeLists.txt).
#

# Unified event queue (ring buffer for mouse+keyboard events)
target_sources(big_kernel_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/event.cpp
)

# F13-B Cinux host adapter + input init + desktop launch
target_sources(big_kernel_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/host_cinux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gui_init.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/desktop_launch.cpp
)
