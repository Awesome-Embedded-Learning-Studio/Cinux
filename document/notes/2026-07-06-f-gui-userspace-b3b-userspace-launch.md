# F-GUI-USERSPACE 批3b — production 用户态 GUI 启动（fork+execve 替 gui_worker）

> 2026-07-06。worktree `worktree-gui-userspace`。代码 commit `62ae31d`（+ assemble host cp）。
> production 内核 fork+execve 用户态 GUI host（替 gui_worker 内核线程）。host 接 /dev/event0 真输入。桌面验证用户启 GUI。
> 接批3a（host adapter + smoke）。下接批4（host_cinux 删 / gui_worker 完全退役）。

## 范围

### step1：host poll_event + dispatch_event（接 /dev/event0）
- `user/cinux_gui_host/main.cpp` 加：
  - `host_poll_event`：`poll(0)` 非阻塞查 /dev/event0 POLLIN → read kernel Event → 转 EventHeader + PointerPayload（mouse）/ KeycodePayload（key）（照 host_cinux cinux_poll_event 行 91-150）
  - `host_dispatch_event`：wm.process_pointer / desktop.dispatch_key（照 host_cinux 行 157-191）
  - main `open("/dev/event0", O_RDONLY)`（ev_fd<0 时 poll_event no-op，降级）
  - Host 表 poll_event/dispatch_event 填

### step2：production fork+execve（替 gui_worker）
- `kernel/gui/desktop_launch.cpp`：
  - `launch_userspace`：gui_start（mouse + kbd listener，双写 /dev/event0，批2）+ fork+execve `/cinux_gui_host 0`（forever）。替 TaskBuilder gui_worker_thread。范式照 main_test smoke fork+execve+AddressSpace。
  - `handoff_framebuffer_to_gui`：简化（不构 host_cinux，只 detach console；fb init 在 main.cpp set_system_framebuffer）。
  - `gui_worker_thread` 退役（删）。host_cinux.cpp 成 dead code（批4 删）。

### assemble：host 进 buildroot gcc rootfs
- `scripts/assemble_gcc_rootfs.sh`：+ cp /cinux_gui_host（build/musl/cinux_gui_host）到 staging。host 静态 musl 自包含，进 buildroot gcc rootfs 不冲突（glibc/gcc closure 共存；两 ldso 共存 busybox musl + gcc glibc + host 静态 musl）。

## 验证

- **console gate**（test kernel，零回归）：run-kernel-test-all 两腿 **1906/0** + cinux_gui_host 5/5 PASS（poll 不阻塞 + readback）+ fb/input 5/5。
- **desktop_launch 编译**：worktree build GUI=ON，big_kernel_common 编 desktop_launch.cpp.o（fork+execve 编译验过）。
- **production 桌面验证**（用户启 GUI run，buildroot gcc rootfs 含 host）：待用户。

production 验证 configure（worktree 或主仓 build）：
```
cmake -B build -DCINUX_GUI=ON -DCINUX_BUILD_TESTS=OFF \
      -DCINUX_ROOTFS_PROFILE=buildroot \
      -DCINUX_ROOTFS_BUILDROOT_IMG=build/rootfs-gcc.ext2
tools/musl/build-cinux-gui-host.sh            # /cinux_gui_host ELF
cmake --build build --target assemble-gcc-rootfs  # rootfs-gcc.ext2 含 host
cmake --build build -j$(nproc)                # big_kernel
cmake --build build --target run              # 启 GUI(VNC :0)
```
桌面：Window + Label "Hello Cinux-GUI"。用户可同时 gcc 编译测试桌面可靠性（GUI 进程 + gcc 并发，验用户态 host 不崩）。

## 设计要点

- **fork+execve 替 gui_worker**：production 内核 init（launch_userspace）fork child（pid2 用户态 GUI host）+ parent（pid1 kernel_init_thread）exit_current。child open /dev/fb0 + /dev/event0 + 构 widget + pump forever。gui_worker 内核线程不再 TaskBuilder。
- **poll_event 非阻塞**：poll(0) 查 POLLIN（空时立即返 false）→ pump 不卡。read 只在 POLLIN 时（event 有，立即返）。CinuxOS sys_poll F8-M5 真 poll ✅（InputEventDeviceOps poll_events 返 kPollIn，批2）。
- **host_cinux dead code**：kernel host adapter（host_cinux.cpp）不再被 handoff 调。编（CINUX_GUI block）但不跑。批4 删。
- **buildroot gcc rootfs + host**：host 静态 musl，进 glibc/gcc closure rootfs 不冲突。

## follow-up（批4）

- host_cinux.cpp 删（完全退役）。
- Shell icon spawn（点图标 fork+execve /bin/sh，PTY 真终端）—— host desktop spawn callback（HostDesktop）。
- gui_worker 退役后 usb poll：老 gui_worker 调 usb::poll_input；用户态 host 不 pump usb。USB 鼠标靠 ISR push（批2 InputEventDevice）。OK（nested-KVM MSI-X 不可靠时 USB 鼠标可能不响应，靠 PS/2 mouse ISR）。
