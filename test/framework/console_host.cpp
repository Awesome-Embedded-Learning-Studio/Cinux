/**
 * @file    console_host.cpp
 * @brief   The console face tests link instead of the real screen.
 *
 * Net sources print through cinux::console when a wire surprises
 * them; capability tests never draw, so the face swallows the bytes.
 * One shared stub replaces the per-test copies.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup test_framework
 * @copyright Copyright (c) 2026
 */

#include <cstdint>

namespace cinux::console {

void PutString(const char* /*text*/, bool /*newline*/) {}

}  // namespace cinux::console
