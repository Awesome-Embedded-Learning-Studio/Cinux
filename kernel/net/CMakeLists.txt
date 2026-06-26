#
# kernel/net/CMakeLists.txt
#
# @brief F7 network L3 stack -- NetDevice seam + NetStack dispatch + ARP/IPv4/ICMP.
#
# §14 file gate like drivers/net: the L3 stack compiles only under CINUX_NET.
# The core (net_stack.cpp) is pure logic -- ZERO kprintf, no kernel runtime
# dependency -- so host unit tests link it directly via
# add_cinux_integration_test() in test/CMakeLists.txt without dragging in
# big_kernel_common (proves the stack is host-buildable + driver-agnostic).
#

if(CINUX_NET)
    target_sources(big_kernel_common PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/net_stack.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/arp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ipv4.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/icmp.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/loopback_device.cpp
    )
endif()
