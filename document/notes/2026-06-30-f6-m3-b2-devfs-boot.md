# F6-M3 批2:DevFS boot 接线 — SerialConsoleSink + 挂载 /dev

**日期**:2026-06-30
**里程碑**:F6-M3 DevFS(boot 接线 + 收官)
**分支**:`worktree-f6-m3-devfs`
**commit**:`84cd8cb`

## 背景

批1 的 DevFs 核心 + 设备 ops 已就位(host/kernel 双测绿),但生产 boot 未挂载 `/dev`。批2 接 boot:`/dev/console` 走真 serial 输出,kernel boot 自动 mount `/dev`。

## 做了什么

1. **[devfs_init.cpp](../../kernel/fs/devfs_init.cpp)(新,kernel-only)**:
   - `SerialConsoleSink : CharSink`(`serial_{SERIAL_COM1}` 逐字节 `putc`,照 kprintf serial sink 范式 [kprintf.cpp:46](../../kernel/lib/kprintf.cpp#L46) `static Serial g_serial(SERIAL_COM1)`)。
   - 静态 `g_devfs_sink` / `g_devfs`(boot 永驻;VFS mount 表持 DevFs 指针)+ `devfs::init()` 钩子(`mount()` + `vfs_mount_add("/dev", ...)`)。
2. **[init.cpp](../../kernel/proc/init.cpp)**:ext2 挂 `/` 后调 `cinux::fs::devfs::init()`(照 ext2 范式,boot 装配一行)。
3. **[devfs.hpp](../../kernel/fs/devfs.hpp)**:加 `namespace devfs { bool init(); }` 声明(kernel-only impl)。
4. CMake:devfs_init.cpp 进 `big_kernel_common`(不进 host 测)。

## 设计要点

- **§14 文件 gate**:devfs_init.cpp(kernel-only,用 Serial/kprintf)与 devfs.cpp(host 可链,零 kernel I/O)分离;CMake 决定编不编,源码零 `#ifdef`。host 测不 link devfs_init.cpp → Serial 依赖不进 host。
- **Serial 范式对齐**:不重新 init COM1(boot 早期已 init),`SerialConsoleSink` 只 `putc`(同 kprintf `g_serial`)。
- **boot 接线最小侵入**:init.cpp 只加一行 `devfs::init()` + 一 include;三路并行协调点仅 CMakeLists(交接栅栏)。

## 陷阱

- **`make run` 串口到 stdout**:run target 配 `-serial stdio`([qemu.cmake:30](../../cmake/qemu.cmake#L30)),boot 序列在 stdout(`[DEVFS] mounted ...`),不是 debug.log(`-debugcon file:debug.log` 是 debug port,空)。
- **run-kernel-test 不覆盖 boot 接线**:test kernel 用 `main_test.cpp`(不调 `kernel_init_thread` 的 `devfs::init()`),故批2 boot 接线靠 `make run` 冒烟验证,run-kernel-test-all 只证 devfs_init.cpp link 进 test kernel 不破。

## 验证

- **make run 冒烟**(生产 big_kernel boot):`[VFS] ext2 mounted at /` → `[DEVFS] mounted at /dev (3 nodes)` → GUI Desktop + xHCI running,零 panic。
- **run-kernel-test-all 两 leg 回归 ALL PASSED**。

## 范围栅栏回顾

`/dev/console` 本批 write→serial(基础节点);接 ConsoleTty 真 stdin / `/dev/tty` / PTY 是 F10-M3 TTY Phase2,显式推迟。mknod / 块设备 / ProcFS / tmpfs / ext4 留 F6 后续。**F6-M3 基础节点收官**(待用户 push/PR)。
