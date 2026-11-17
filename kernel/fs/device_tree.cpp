/**
 * @file    device_tree.cpp
 * @brief   The one device-tree seat both ports assemble through.
 *
 * Binding the device model to a devfs, registering the console,
 * framebuffer and pty faces, and seating the two input sources used
 * to be lines the x86 singleton ran inline. The steps are neutral;
 * only the input sources are the port's business. This face runs the
 * shared order once, so a new port gets the whole tree — ptmx
 * included — with its two event streams as the only words it adds.
 *
 * @author  Charliechen114514
 * @date    2026-11-13
 * @version 0.1.0
 * @since   0.1.0
 * @ingroup kernel_fs
 * @copyright Copyright (c) 2026
 */

#include "kernel/fs/device_tree.hpp"

#include "cinux/assert.hpp"
#include "kernel/drivers/device/device.hpp"
#include "kernel/fs/devfs/console_node.hpp"
#include "kernel/fs/file_table.hpp"
#include "kernel/fs/devfs/devfs.hpp"
#include "kernel/fs/devfs/fb_node.hpp"
#include "kernel/fs/devfs/input_node.hpp"
#include "kernel/fs/devfs/pty_node.hpp"

namespace cinux::fs {

void SeatDeviceTree(DevFs& devfs, driver::EventStream& primary_input,
                    driver::EventStream& pointer_input) {
    driver::DeviceModel::self().bind(devfs);
    RegisterBuiltinDevices();
    RegisterConsoleDevice();
    RegisterFramebufferDevice();
    RegisterPtyDevices();
    RegisterInputDevices(primary_input, pointer_input);
}

base::Result<void> SeatConsoleStreams(Vfs& vfs, FileTable& table) {
    const base::Result<FsView> kView = vfs.resolve("/dev/console");
    if (!kView.ok()) {
        return kView.error();
    }
    const FsView&              flat  = kView.value();
    const base::Result<FsNode> kNode = flat.ops->lookup(flat.self, "console");
    if (!kNode.ok()) {
        return kNode.error();
    }
    flat.ops->retain(flat.self, kNode.value());
    const base::Shared<OpenFile> kStream{new OpenFile(flat.ops, flat.self, kNode.value())};
    if (!kStream.holds()) {
        flat.ops->release(flat.self, kNode.value());
        return base::KernelError::kOutOfMemory;
    }
    kStream->status_flags = kFsReadWriteStatus;
    for (unsigned long seat = 0; seat < 3; ++seat) {
        const base::Result<unsigned long> kSeated = table.adopt_at(kStream, seat);
        if (!kSeated.ok()) {
            return kSeated.error();
        }
    }
    return {};
}


}  // namespace cinux::fs
