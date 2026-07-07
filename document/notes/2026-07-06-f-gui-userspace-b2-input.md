# F-GUI-USERSPACE 批2 — 输入到用户态（/dev/event0）

> 2026-07-06。worktree `worktree-gui-userspace`。代码 commit `45378a1`。
> 把鼠标/键盘事件送到用户态：evdev-like 字符设备 `/dev/event0`，ISR push → MPSC ring → 用户 read/poll。
> 接批1（fb mmap，`cc72585`）。下接批3（用户态 GUI 进程）。

## 范围

- **新建 `kernel/drivers/input/input_event_device.{hpp,cpp}`**：`InputEventDevice` 单例（MPSC `RingBuffer<Event,128>` + `Spinlock` + `read_waiters_`）+ `InputEventDeviceOps : InodeOps`（read 阻塞 + poll + detach）+ 工厂 `input_event_device_ops()`。
- **mouse 双写**：`mouse.cpp` 7 处 `g_event_queue_.enqueue(ev)` 后加 `InputEventDevice::push_event(ev)`（replace_all，`update_absolute` 内 move/down/up 各处）。
- **keyboard 双写**：`gui_init.cpp` `on_key_event`（已把 KeyEvent 包成 `gui::Event` 推 GUI queue）加一行 `push_event(gui_ev)`。零侵入 `keyboard.cpp`——`key_listener_` 单槽已被 `on_key_event` 占，不再 listener，改在 listener 内顺便 push。
- **DevFs**：`devfs_init.cpp` 注册 `/dev/event0`（flat；`/dev/input/event0` 要扩 `lookup_child`，留 follow-up）。
- **ring3 smoke**：`tools/musl/input_event_test.c`（mirror Event 布局，open + read 2 event 验 type/payload）+ `build-input-event-test.sh` + 注入链（`CINUX_INPUT_SMOKE` option + `INPUT_EVENT_TEST_ELF` + `create_ext2` 参数 + `main_test` smoke 段 fork+execve + `smoke_entry` mock push）。

## 设计要点

- **MPSC**：mouse IRQ12 + keyboard listener 两生产者 → 独立 `RingBuffer<Event,128>` + `Spinlock`（不复用 GUI 的 `EventQueue`，那是无锁 SPSC，多生产者竞态）。
- **read 阻塞**：PTY 范式（`prepare_to_wait` 在 `irq_guard` 内，`schedule_blocked` 在锁外，lost-wakeup 闭合）。evdev 语义：`count < sizeof(Event)` → EINVAL。
- **poll**：Pipe 范式（`!empty → kPollIn` + `wait_enqueue` 注册 waiter + `*registered=true`）。
- **ISR 安全**：`push_event` 只 `wake_all`（`Scheduler::unblock` 设 runnable），不 inline `schedule`（sti-in-syscall #DF 铁律，同 PTY/pipe/NVMe ISR）。

## 三个踩坑（按还原顺序）

### 1. `CINUX_INPUT_SMOKE` option 没进 compile def（假绿）

加 `option(CINUX_INPUT_SMOKE ...)` 不够 —— `main_test.cpp` 的 `#ifdef CINUX_INPUT_SMOKE` 要编译器看到 `-DCINUX_INPUT_SMOKE`。CMake `option()` 只控 CMake 逻辑（if），**不传 `-D` 给编译器**。要进 `CINUX_COMPILE_DEF_OPTS`（`options.cmake`，auto-map 到 `big_kernel_common` PUBLIC compile def，`kernel/CMakeLists.txt:131` 的 foreach）。

漏了 → 主门 `#if defined(... CINUX_INPUT_SMOKE)` false → input smoke 段 `#else bool input_ok=true` 假绿（没真跑，fb smoke 还跑但 input 段编译掉）。第一次 run-kernel-test-all exit=0 但 log 无 `[F-GUI] input ring-3 smoke`。

修：`CINUX_COMPILE_DEF_OPTS` 加 `INPUT_SMOKE`。这是本批最隐蔽的坑——option≠def，CINUX_FB_MMAP_SMOKE 能跑是因为它早就在 `CINUX_COMPILE_DEF_OPTS` 里。

### 2. CMake 空参折叠 + EXT2 不重建（execve ENOENT）

`create_ext2` 调用 `${GCC_ROOT}` 空（`CINUX_GCC_TOOLCHAIN=OFF`）时，CMake 无引号 `${VAR}` 折叠该 arg（不占 argv 槽），`${INPUT_EVENT_TEST_ELF}` 落到 $9（本应 ${10}），`create_ext2_disk.sh` 的 `INPUT_EVENT_TEST_ELF="${10}"` 拿空 → cp 段跳过 → ext2 没 `input_event_test` → child execve ENOENT（`execve(/input_event_test) failed: -2`）。

叠加 `EXT2_IMAGE` 的 `add_custom_command` `DEPENDS` 不含新 ELF → ext2 不重建（批1 旧 ext2，即使参数修了也不重跑）。

修：
- `qemu.cmake` 两处调用 `${INPUT_EVENT_TEST_ELF}` 放 `${GCC_ROOT}` **前**（避空参折叠，$9=INPUT，${10}=GCC_ROOT 空）。
- `create_ext2_disk.sh` 参数顺序调（`INPUT_EVENT_TEST_ELF="$9" GCC_ROOT="${10}"`）。
- `rm build/ext2.img` 强制重建（一次性；以后参数变 CMake 检测不到，得手动 rm 或加 DEPENDS）。

同族 GCC_ROOT 空参折叠坑（批1 fb_mmap 放 $8 在 GCC_ROOT $9 前躲过；本批 INPUT 放 GCC 后中招）。

### 3. sys_read staging buffer（copy_to_user 拒 kernel 地址）

`InodeOps::read` 收的 `buf` 是 **kernel staging buffer**（`sys_read` 提供），**不是 user pointer**。`sys_read.cpp:11/50/108`：`do_read_kernel` 把数据读进 kernel staging buf（阻塞在这），之后 `sys_read` 自己 `copy_to_user` staging→user。console TTY / PTY / pipe read 同范式（`devfs_init.cpp:62` 注释明说）。

我误用 `cinux::user::copy_to_user(buf, &ev, sizeof)` → `is_user_vaddr` 拒绝 kernel 地址 → `copy_to_user` 返 false → read 返 Fault → sys_read 返 -EFAULT → test `n=-1 != sizeof` → `exit(2)`。

仪器实证：`[input] read pop type=3 copy=0 ret=24`（pop 成功但 copy=0）。对比 fb_dev `ioctl` 直接 `copy_to_user`（`ioctl` 路径传 user arg，不是 staging）—— read 与 ioctl 的 buf 性质不同。

修：`std::memcpy(buf, &ev, sizeof(ev))` 写 staging（kernel→kernel），sys_read 之后自己 copy_to_user。kernel `std::memcpy` + `<cstring>`（`fork.cpp`/`clone.cpp` 范式，Cinux-Base `<cstring>`）。

## 验证

`run-kernel-test-all` 两腿（单核 + `-smp 2`，VNC :5 避让主会话，跑完 sed 回 :0；不用 `git checkout`——会还原 qemu.cmake 的 smoke 注入）：

- ring-0 **1906 PASS / 0 FAIL**（零回归）
- `[F-GUI] smoke: input_event_test 5/5 iters PASS -> PASS`（两腿）
- `[F-GUI] smoke: fb_mmap_test 5/5 iters PASS`（两腿，批1 不回归）
- exit=0

smoke 链路：`smoke_entry` push `MouseMove(123,45)` + `KeyDown('A')` → fork+execve `/input_event_test` → open `/dev/event0` → read 2 event → `memcpy` staging → sys_read `copy_to_user` → test 验 type + payload → exit 0。

## follow-up

- `/dev/input/event0` 子目录（现 flat `/dev/event0`，DevFs 扁平不支持子目录，要扩 `lookup_child`）。
- input smoke 是 mock push（test kernel 无真鼠标）；production GUI 跑真鼠标/键盘时端到端验（批3 用户态 GUI 进程）。
- poll path 未跑机制测（smoke 只 read）；批3 用户态 host 用 poll 时再验。

下：批3 用户态 GUI 进程（Cinux-GUI core + CinuxOS host adapter，抄 `linux_fbdev_main`）。
