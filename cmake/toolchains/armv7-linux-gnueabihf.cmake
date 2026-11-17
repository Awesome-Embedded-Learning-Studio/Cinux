# The armv7 userland's cross toolchain: the Arm GNU Toolchain
# (arm-none-linux-gnueabihf, GCC 15.2) installed under
# /opt/arm-gnu-toolchain, found through PATH or CINUX_ARM_TOOLCHAIN.
# GCC 14 is the floor because the desktop sources use C++23 <print>;
# the programs this builds are the ones the port's loader can seat:
# EABI5, hard float, Cortex-A7, ARM state, static.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# The toolchain's home honors the environment so a tree can live beside
# the workspace it serves; PATH resolution covers the bare-machine setup.
if(DEFINED ENV{CINUX_ARM_TOOLCHAIN})
    set(_cinux_arm_bin "$ENV{CINUX_ARM_TOOLCHAIN}/bin")
elseif(EXISTS "/opt/arm-gnu-toolchain/bin")
    set(_cinux_arm_bin "/opt/arm-gnu-toolchain/bin")
else()
    find_program(_cinux_arm_gpp arm-none-linux-gnueabihf-g++ REQUIRED)
    get_filename_component(_cinux_arm_bin "${_cinux_arm_gpp}" DIRECTORY)
endif()
set(CINUX_ARM_TOOLCHAIN_BIN "${_cinux_arm_bin}" CACHE PATH "armhf cross bin")

find_program(CMAKE_C_COMPILER   arm-none-linux-gnueabihf-gcc
             HINTS "${_cinux_arm_bin}" REQUIRED)
find_program(CMAKE_CXX_COMPILER arm-none-linux-gnueabihf-g++
             HINTS "${_cinux_arm_bin}" REQUIRED)
find_program(CMAKE_AR           arm-none-linux-gnueabihf-ar
             HINTS "${_cinux_arm_bin}" REQUIRED)
find_program(CMAKE_RANLIB       arm-none-linux-gnueabihf-ranlib
             HINTS "${_cinux_arm_bin}" REQUIRED)

set(CMAKE_C_FLAGS_INIT   "-mcpu=cortex-a7 -marm -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a7 -marm -mfpu=neon-vfpv4 -mfloat-abi=hard")

if(DEFINED ENV{CINUX_ARM_TOOLCHAIN})
    set(CMAKE_FIND_ROOT_PATH "$ENV{CINUX_ARM_TOOLCHAIN}/arm-none-linux-gnueabihf")
else()
    set(CMAKE_FIND_ROOT_PATH "/opt/arm-gnu-toolchain/arm-none-linux-gnueabihf")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
