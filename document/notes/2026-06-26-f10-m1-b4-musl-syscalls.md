# F10-M1 批4 — 补 musl 所需 syscall + dispatch 未注册返 -ENOSYS

> 里程碑：F10-M1 用户态运行时 / musl 静态移植（批4）。分支 `feat/f10-musl`，commit `8bab7a2`。
> 验证：`run-kernel-test` 950/0（+5 新测）+ 全量 `cmake --build build` 绿 + `test_host` 55/0。

## 背景与方法

批1-3 把 syscall ABI / 结构体布局 / execve 初始栈 auxv 对齐了 Linux。批4 补 musl **真正会发**的
那些 syscall —— 不拍脑袋，照旧方法论读 musl 源码（`src/env/__libc_start_main.c` / `__init_tls.c` /
`__stdio_write.c` / `src/exit/_Exit.c` + `arch/x86_64/bits/syscall.h.in`）+ Linux UAPI 实证。

追 `__libc_start_main → __init_libc → __init_tls → __init_tp → printf → exit` 全链，定位每个阶段的
真实 syscall。结论：handoff 头条列的 openat/newfstatat 其实**不是**启动硬阻塞，真正卡 musl 的是
另两个 handoff 漏掉的。

## recon 关键结论（影响打法 / 推翻 handoff 头条）

| 阶段 | musl 真实调用 | 结论 |
|---|---|---|
| TLS 初始化 | `arch_prctl(ARCH_SET_FS, tp)` **158** | **头号硬阻塞**：musl 第一次 `%fs:0` 访问就崩。CinuxOS 早有 `set_tls_base()`（context_switch 存取 fs_base、clone SETTLS），直接复用 |
| TLS 初始化 | `set_tid_address(&lock)` **218** | musl `__init_tp` 无条件调，返值当 `tid`；不实现返 0/错值污染 TCB |
| **printf 输出** | `writev(fd, iov, 2)` **20** | **第二号硬阻塞**：musl `__stdio_write` 用 `SYS_writev` 聚合「未刷前缀+新字节」两段 iov，**不是 `write(1)`**！`__stdout_write` 首写还先 `ioctl(TIOCGWINSZ)` 16 探是否 tty。**不实现 writev，printf 静默无输出** |
| 退出 | `exit_group` **231** → 兜底循环 `exit` **60** | exit_group 没实现时 musl 兜底调 `exit(60)`（已有），**非硬阻塞**但该补 |
| 文件 | `openat` **257** / `newfstatat` **262** | 纯 stdout hello 用不到；真程序 I/O 才需要 |

**两个 latent bug**（公共接口，批4 正好改）：

1. **dispatch 未注册/越界返裸 `-1`**（`syscall.cpp`）：musl 把返回值 `-4095..-1` 当 `-errno`，所以
   裸 `-1` 被读成 `errno=EPERM(1)`。应是 `-ENOSYS(38)` 才能让 musl 探测（rseq/prlimit/…）优雅降级。
2. **handoff 误记 clock_gettime=99**：读 musl `syscall.h.in` 实证 **99 是 sysinfo，clock_gettime=228**。
   —— 这就是为什么要拉源码不猜。

**复用度**：wait4(61) 其实**已实现**（`sys_waitpid` 在 61 号位，第 4 参 rusage 忽略，musl `waitpid()`
传 NULL 正好命中）。`set_tls_base`、`sys_write`（writev 逐 iov 委派）、`sys_exit`（exit_group 委派）、
`split_pathname`/`create`（openat O_CREAT）全是现成。

## 改动

### 公共接口层

- `SYSCALL_TABLE_SIZE` 256→**512**：openat=257 / newfstatat=262 超原表会越界。覆盖所有 Linux x86_64
  号（最大 ~440）+ 余量。
- `syscall_dispatch` 未注册/越界 → **`-cinux::kEnosys`**（原裸 `-1`）。
- `errno.hpp` 加 `kEnotty = 25`（ioctl）。
- `syscall_nums.hpp` 枚举重排按号序 + 新增 lseek/ioctl/readv/writev/arch_prctl/set_tid_address/
  clock_gettime/exit_group/openat/newfstatat。

### 新 syscall（10 个）

**启动/输出硬阻塞**：
- `arch_prctl(158)`（sys_arch_prctl.{hpp,cpp}）：`ARCH_SET_FS` → `set_tls_base(addr)` + 记
  `task->ctx.fs_base`；`ARCH_GET_FS` 读回；GS/其它返 `-EINVAL`。
- `writev(20)` / `readv(19)`（sys_iov.{hpp,cpp}）：定义 `kiovec{iov_base,iov_len}`，校验 iov 数组，
  逐段委派 `sys_write`/`sys_read` 累加字节数；段错时已传返部分计数，否则返错；iovcnt=0/>1024 拒绝。

**进程/退出**：
- `set_tid_address(218)`（sys_set_tid_address.{hpp,cpp}）：`task->clear_child_tid = tidptr`（复用现有
  cleartid 字段，`task_exit_cleartid` 已会清零+唤醒）+ 返 `task->tid`。
- `exit_group(231)`（并入 sys_exit 家族）：委派 `sys_exit(code)`。单线程 musl 等价；多线程「全杀线程组」
  是 follow-up（一旦 CLONE_THREAD 程序上场）。

**文件/时间**：
- `openat(257)`（并入 sys_open 家族）：AT_FDCWD(-100) cwd 解析（musl 只用这个；真 dirfd 待 per-fd 路径
  追踪，现 cwd 兜底，已注释为限制）；lookup 失败 + O_CREAT → `split_pathname`+`create`+重 lookup；
  访问模式低 2 位 → OpenFlags；alloc fd。
- `newfstatat(262)`（并入 sys_stat 家族）：抽 `fill_user_stat(inode, buf)` helper；AT_EMPTY_PATH 走
  dirfd 的 fstat；否则 cwd 解析 stat。
- `lseek(8)`（sys_lseek.{hpp,cpp}）：`file->offset_lock_` 下按 SEEK_SET/CUR/END 改 `file->offset`，负位
  拒绝；无 VFS entry 的 legacy fd 走不到（fd 校验 `-kEbadf`）。
- `ioctl(16)`（sys_ioctl.{hpp,cpp}）：最小，全返 `-ENOTTY`（musl `__stdout_write` 的 TIOCGWINSZ 探测
  只需非零即判非 tty 设行缓冲；设备特定 ioctl 待驱动需要时加）。
- `clock_gettime(228)`（sys_clock_gettime.{hpp,cpp}）：CLOCK_REALTIME/MONOTONIC 用 `PIT::get_uptime_ms()`
  填 `timespec{tv_sec=ms/1000, tv_nsec=(ms%1000)*1e6}`；坏 clock `-EINVAL`。CinuxOS 暂无 wall clock，
  realtime 也是 boot-relative 兜底（待 RTC 源接）。

### 测试（test_syscall.cpp，+5）

writev 2 段求和(3+4=7) / writev 零 iovcnt 拒绝 / ioctl 返 -ENOTTY / clock_gettime 填充 tv_sec≥0 /
clock_gettime 坏 clock 拒绝。同步改 3 个旧 dispatch 测试：未注册、越界（256→512/1024）、表大小
(256→512) 全改断 -ENOSYS/-38；`test_dispatch_max_valid` 改注册 511。arch_prctl/set_tid_address/
exit_group 是纯委派两三行，靠 code review + 批6 端到端覆盖（harness 里改当前 task fs_base/cleartid
有副作用风险，不做 live 测）。

## 踩坑

1. **`kEnosys` 在 `namespace cinux`**：`sys_*.cpp` 裸用 `-kEnoent` 是因它们在 `cinux::syscall` 内层；
   但 `syscall_dispatch` 是 `extern "C"` 全局作用域，得写 `-cinux::kEnosys`。IDE 早期给矛盾提示
   （先说不在 cinux、加了 include 后又建议 cinux::），根因是没 include `errno.hpp` —— 先 include 再判断。
2. **写 include 块时手滑**：误把 `sys_ioctl.hpp` 写成 `sys_iotcl.h` + 截断了多个既有 include → 一堆
   undeclared。教训：大段 include 替换后立刻增量编一次抓全。
3. **空 if 体**：openat 初版写了 `if (dirfd != kAtFdcwd) { /* 注释 */ }` 空 body，别扭；改成纯
   `(void)dirfd; (void)kAtFdcwd;` + 注释说明限制。

## follow-up

- **exit_group 真线程组终止**：当前 == exit。多线程 musl 程序上场时需遍历 group 逐个 reap。
- **per-fd 路径追踪**：openat/newfstatat 真 dirfd（非 AT_FDCWD）现在 cwd 兜底；要 *at 完整语义需给
  File/dirfd 挂目录路径。
- **ioctl 设备特定**：现全 -ENOTTY；驱动需要时按 request 分发。
- **RTC wall clock**：clock_gettime REALTIME 是 boot-relative；接 RTC 后填真实 epoch。
- **批6 端到端**：批4 的 writev/arch_prctl 等是批3 launch_user_program 跑 musl hello 的真验证网（批4
  只 host/in-kernel 单测了可测的部分；真 musl printf 走 writev 的端到端验证在批6）。

## 批4 之后

批5 musl 源码编译 + sysroot（configure+make → libc.a/crt1.o，`-static` 编 hello）；批6 端到端 QEMU
跑通 musl hello（批3 launch + 批4 syscall 闭环的真验证）。
