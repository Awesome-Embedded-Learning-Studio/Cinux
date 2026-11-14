# The armv7 userland's cross toolchain: Ubuntu's armhf glibc cross
# tree, unpacked under ~/.local/opt/arm-cross14. GCC 14 is the floor here
# because the desktop sources use C++23 <print>, and the programs this
# builds are the ones the port's loader can seat: EABI5, hard float,
# static, and linked into the user window.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CINUX_ARM_CROSS_ROOT "$ENV{HOME}/.local/opt/arm-cross14" CACHE PATH "armhf cross tree")
set(CMAKE_C_COMPILER   "${CINUX_ARM_CROSS_ROOT}/usr/bin/arm-linux-gnueabihf-gcc-14")
set(CMAKE_CXX_COMPILER "${CINUX_ARM_CROSS_ROOT}/usr/bin/arm-linux-gnueabihf-g++-14")
set(CMAKE_AR           "${CINUX_ARM_CROSS_ROOT}/usr/bin/arm-linux-gnueabihf-ar")
set(CMAKE_RANLIB       "${CINUX_ARM_CROSS_ROOT}/usr/bin/arm-linux-gnueabihf-ranlib")

set(CMAKE_C_FLAGS_INIT   "-mcpu=cortex-a7 -marm -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a7 -marm -mfpu=neon-vfpv4 -mfloat-abi=hard")

set(CMAKE_FIND_ROOT_PATH "${CINUX_ARM_CROSS_ROOT}/usr/arm-linux-gnueabihf")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
