/**
 * @file libs/ext2/ext2_mutex.hpp
 * @brief Sleepable ext2 mutex with a host-test implementation
 */

#pragma once

#ifdef CINUX_HOST_TEST

#    include <mutex>

namespace cinux::fs {

/// Host counterpart of the kernel's sleepable mutex.
class Ext2Mutex {
public:
    [[nodiscard]] auto guard() { return std::lock_guard<std::mutex>(mutex_); }

private:
    std::mutex mutex_;
};

}  // namespace cinux::fs

#else

#    include "kernel/proc/sync.hpp"

namespace cinux::fs {

using Ext2Mutex = cinux::proc::Mutex;

}  // namespace cinux::fs

#endif
