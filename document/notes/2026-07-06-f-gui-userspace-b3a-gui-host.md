# F-GUI-USERSPACE 批3a — 用户态 GUI host adapter（core 编进 musl ELF）

> 2026-07-06。worktree `worktree-gui-userspace`。代码 commit `fe9ba6f`。
> 把 Cinux-GUI host-neutral core（21 个 freestanding C++ 源）编进用户态 musl ELF，host adapter（open /dev/fb0 + mmap + Widget 树 + pump + readback）fork+execve 跑。证明 core 用户态可编 + Host ABI 接通 + fb 渲染管线通。
> 接批2（/dev/event0）。下接批3b（production 启动 + 桌面）。

## 范围

- **新建 `user/cinux_gui_host/main.cpp`**：host adapter（抄 `host/linux_fbdev_main.cpp` + `kernel/gui/host_cinux.cpp` 换 CinuxOS syscall）。HostState（fb mmap + Widget 树）+ Host ABI 表（flush / render_frame / now_ms / alloc / free / log；poll_event=NULL 避 event0 阻塞）+ main（open /dev/fb0 + ioctl + mmap + 构 Widget + GuiCore + pump N + readback 中心像素非 0 → exit 0）。
- **新建 `user/cinux_gui_host/crt_stub.cpp`**：operator new/delete 全 8 重载（→ malloc/free）+ `__cxa_pure_virtual`（Widget 有纯虚）。
- **`tools/musl/build-cinux-gui-host.sh`**：g++ 静态 musl + core 21 源 + freestanding C++（-fno-rtti -fno-exceptions；core 零 libstdc++ 依赖，Plan agent nm 验证）。
- **注入链**：`CINUX_GUI_HOST_SMOKE` option（+ `CINUX_COMPILE_DEF_OPTS` 避批2 踩坑①）+ ELF + create_ext2 $10（放 GCC_ROOT 前避批2 踩坑②）+ main_test smoke fork+execve `/cinux_gui_host 100`。

## 设计要点

- **Host ABI 唯硬缝**（core/host.hpp）：core 对 host 一无所知，只调表里函数。换 host = 换表填充。用户态 host 跟内核 host_cinux 实现同 ABI，不同后端（host_cinux 用 kernel 设备；用户态 host 用 syscall + mmap）。
- **flush 显示模型**：core 拥 staging buffer，host render_frame 画 staging + 报 dirty rect，host flush 把 dirty rect blit 到 fb mmap（IoPhys VMA，批1 铺的路）。
- **poll_event=NULL**：smoke 不验交互（test kernel 无真鼠标；/dev/event0 read 阻塞会卡 host）。批3b 加 O_NONBLOCK read 接真输入。
- **spike 先行**：先极简 main（Host 全 NULL + GuiCore + pump 1 + exit 0）验编译（core 零 libstdc++ 依赖）。绿后补全正式 main（Widget + fb + readback）。隔离编译风险——Plan agent 审计 + nm 验证已高置信，spike 实践确认。

## 三个踩坑（按还原顺序）

### 1. `-ffreestanding` 下 `<stdlib.h>` 不 bring `::free`

crt_stub.cpp `#include <stdlib.h>`（C 风格）但 g++ -ffreestanding 报 `free not declared`。freestanding 不保证 hosted header 完整（`<cstdlib>` 保证 `std::` 但非 `::`）。解：malloc/free/abort 用 `extern "C"` 直接声明（不依赖 header）。

### 2. `std::align_val_t` 缺 `<new>`

aligned operator new 重载用 `std::align_val_t`，但 -ffreestanding + `<stddef.h>` 不带。需 `#include <new>`（freestanding header，提供 `std::align_val_t`，不拖 libstdc++）。

### 3. `__isoc23_strtoul`（glibc C23 wrapper，musl 没）

正式 main 用 strtoul 解析 argv pump 次数。host glibc `<stdlib.h>`（GCC 16）把 strtoul 包成 C23 wrapper `__isoc23_strtoul`（链接期找这符号），musl libc.a 只提供 `strtoul` → undefined reference。其他 libc 函数（printf/malloc/open/mmap/clock_gettime）是 C90/POSIX，C23 没改 ABI，无此问题。解：手写 parse（逐字符累积），不用 strtoul。

## 验证

`run-kernel-test-all` 两腿（单核 + -smp 2，VNC sed :5→:0 避让）：
- ring-0 **1906 PASS / 0 FAIL**（零回归）
- `[F-GUI] smoke: cinux_gui_host 5/5 iters PASS -> PASS`（两腿，readback 中心像素非 0）
- fb_mmap_test 5/5 + input_event_test 5/5（批1/2 不回归）
- exit=0

smoke 链路：smoke_entry fork+execve `/cinux_gui_host 100` → child open /dev/fb0 + ioctl + mmap（IoPhys VMA，批1）→ 构 Widget 树 + GuiCore（operator new→malloc stub）→ pump 100 次（render_frame 画 staging + flush blit 到 fb mmap）→ readback fb 中心像素非 0 → exit 0。

## follow-up（批3b）

- production 启动：`launch_userspace()`（desktop_launch.cpp）改 fork+execve `/cinux_gui_host 0`（forever）替 gui_worker 内核线程。
- gui_worker 退役（host_cinux.cpp 批4 删）。
- poll_event 接 /dev/event0（O_NONBLOCK read 或 poll，避阻塞）+ dispatch_event（wm.process_pointer / desktop.dispatch_key）。
- **桌面验证（图标 + 光标 + 鼠标响应）用户启 GUI QEMU**（`gui-verification-user-starts-always`）。
