# F-USABILITY — 可用性验证弧

> 横切弧（与 F-VERIFY / F-INFRA / F-ECO 同档）。立项 2026-07-02，分支 `feat/f-usability`，从干净 main `6ba4a88`。

## 目标

从「手搓 rootfs 夹具」升级到「**证明 CinuxOS 真能跑真实 Linux 用户态**」：
Buildroot 工业级 rootfs + 进 shell 跑真实工具 + gcc/g++ 冒烟（CinuxOS 上 `gcc hello.c → ./a.out`）。

## 用户决策（2026-07-02 会话）

1. **Buildroot 只当 rootfs 骨架打包器**：external toolchain + busybox + musl，**不 internal 编 GCC**（那个 CI 顶不住）。
2. **GCC 全拷贝不编译**：`tools/gcc-toolchain/extract.sh`（PR#62 已合 main）从 host 抽 cc1/as/ld + glibc runtime + crt + libgcc + `gcc -H` 头文件闭包，已是工业级实现。
3. **CI = PR 内编 + 强缓存**：cache key = defconfig + overlay hash，命中秒过；profile 是 job 属性，不进 build_type×sanitizer 矩阵。
4. **手搓 `tools/musl/` + `create_ext2_disk.sh` 并存**：留快反馈/调试载体，Buildroot 出问题时能定位是内核还是 rootfs 的锅。
5. **只动 ext2 disk（真 rootfs）**：ext4/ahci 测试夹具 + `build_image.sh` 启动盘不动。

## 可行性（已实证）

- **99 syscall 覆盖** busybox rootfs 硬依赖 56/60，仅缺 `dup3/pipe2/recvmsg/sendmsg`（程序兜底，不挡）。
- **`wait4` 实际在 slot 61**（注册名 `SYS_waitpid`，handler 吃 4 实参，musl `waitpid` 传 `rusage=NULL` 兼容）。别再 grep `SYS_wait4` 字面量漏判。
- **busybox init PID1**（F-ECO B3b）+ **cc1→as→ld→./hello 全自举闭环**（F12-M2 批4）均已跑通。
- **双 libc 共存已 work**：musl 静态 busybox（自洽）+ glibc 动态 cc1（靠 extract.sh 一起拷进来的 glibc `.so` + `/lib64/ld-linux`）。不用二选一。
- **Buildroot 动态 busybox base rootfs 已到 `/ #`**（2026-07-02）：修低地址 `MAP_FIXED`、`PROT_NONE` fault 权限、Linux fd 0/1/2 分配与 PID1 stdio 安装；详见 [note](../../notes/2026-07-02-f-usability-buildroot-busybox-fix.md)。
- **gcc driver 单命令已闭环**（2026-07-03）：Buildroot gcc profile 内
  `gcc -fno-pie -no-pie /hello.c -o /tmp/a.out && /tmp/a.out` 输出 `Hello from GCC!`；
  详见 [note](../../notes/2026-07-03-f-usability-b3-thread-gcc.md)。

## 三层分离（对齐 Buildroot/Yocto image-builder 范式）

```
Buildroot 产出(busybox+musl+目录骨架) ──┐
                                        ├─→ assemble staging ─→ mkfs ─→ rootfs.img
rootfs/overlay/(CinuxOS 专有:inittab    ─┤   (按 profile)
  + 可用性测试脚本 + smoke 源码)         │
extract.sh 产出(GCC 工具链闭包, 批3+) ──┘
```

- `rootfs/buildroot/`：Buildroot defconfig（按 profile：base / gcc / g++）
- `rootfs/overlay/`：checkin 的静态树（定制 inittab、`etc/cinux-usability-test.sh`、`smoke/hello.c`）
- `scripts/assemble_rootfs.sh <profile> <staging>`：合成 staging 目录
- `scripts/pack_rootfs.sh <staging> <img> [fstype]`：纯打包（mkfs.ext2 -d / mkfs.ext4 / squashfs），零内容
- CMake 只消费 `rootfs.ext2`，不知它怎么来

## 阶段（对应批）

| 批 | 阶段 | 范围 |
|----|------|------|
| 0 | 立项 | docs（本 README）+ ROADMAP + PLAN |
| 1 | 0+1 验证+接管 | Buildroot base defconfig；assemble/pack 工具；本地 boot 到 ash；overlay 合并；取代 create_ext2_disk.sh（手搓并存） |
| 2 | 2 可用性测试 | overlay 测试脚本；CI build-rootfs + buildroot-usability job |
| 3 | 3 gcc 冒烟 ✅ | extract.sh 闭包进 rootfs；`gcc hello.c && ./a.out`；CI gcc-smoke job |
| 4 | 4 g++ 冒烟 | 扩展 extract.sh 拷 cc1plus + libstdc++；`g++ hello.cpp` |
| 5 | 5 扩包 | util-linux / coreutils 完整版 / dropbear（按需） |

## 批3 follow-up（登记，留后续）

- **unhandled syscall stub 降噪**：glibc probe（318 getcpu / 435 clone3 / 273 rseq / 334 / 302）现走 ENOSYS fallback（非致命），补 stub 返 ENOSYS 或合理值改善 CI log 可读性。
- **既有信号 SMP 债**：sig_pending/sig_blocked 普通 uint64 无原子，SMP 投递竞态；批3a-2（`fb42a1d`）只修 force_sig 新债（per-task `sig_forced`），既有债（信号子系统全原子化 + siglock）留独立 follow-up。
- **gcc driver 残留**：collect2 fork-exec + pipe 连 cc1/as/ld 完整语义（B4-C2 + 批3 修了大头）；残留 pipe EOF 对齐 BusyBox ash（阶段2 caveat：`echo | cat` 真管道留 follow-up）。
- **CI gcc-smoke 真验**：批3b-2（`f9e35cc`）写完结构，push 后验 buildroot 下载/编译 + assemble + gate（顺带补批2 buildroot-usability CI 债——阶段2 只本地闭环）。

## 头号坑：target vs cross 工具链

进 rootfs 的 GCC 必须是 **native**（在 CinuxOS 跑、产 CinuxOS 二进制）。extract.sh 拷的是 host 的 cc1/as/ld —— 它们是 glibc 动态的 host 二进制，靠一起拷进来的 glibc `.so` 在 CinuxOS 跑（已验证）。**绝不能塞 Buildroot 的 cross compiler**（host 程序，CinuxOS 跑不了，一跑 SIGSEGV 且难分辨是内核问题还是工具链塞错）。

详见 PLAN「🔄 F-USABILITY」段。
