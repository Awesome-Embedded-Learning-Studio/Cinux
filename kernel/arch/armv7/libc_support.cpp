/**
 * @file    libc_support.cpp
 * @brief   Freestanding runtime stubs the armv7 image still needs.
 *
 * The C memory primitives moved to the shared kernel/lib/mem.cpp now
 * that the armv7 lane links it; what remains are the atexit
 * registration stubs static constructors ask for.
 *
 * @author  Charliechen114514
 * @date    2026-11-12
 * @version 0.1
 * @since   0.1.0
 * @ingroup kernel_arch
 * @copyright Copyright (c) 2026
 */

#include <cstddef>
#include <cstdint>

#include "cinux/assert.hpp"

// Static objects with constructors make the compiler register a
// destructor with the ARM EABI atexit entry point. The kernel never
// exits, so the registry is accepted and dropped: registration "succeeds"
// and nothing ever runs.
extern "C" void* __dso_handle = &__dso_handle;

extern "C" int __aeabi_atexit(void* /*object*/, void (*)(void*) /*destructor>,
                              void* /*dso_handle*/) {
    return 1;  // registration accepted; the table is intentionally absent
}
