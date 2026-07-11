<div align="center">

# 🐧 Cinux

### x86_64 操作系统 · 现代 C++ 实现 · SMP 多核 · TCP/IP · GUI 桌面 · GCC 自举

[![Version](https://img.shields.io/badge/version-v1.0.0-blue.svg)](document/changelogs/CHANGELOG.md)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![GCC](https://img.shields.io/badge/GCC-11%2B-blue)]()
[![QEMU](https://img.shields.io/badge/QEMU-8.0%2B-orange)]()

一个基于 x86_64 架构的操作系统——从 **Bootloader 到 GUI 桌面、从内核到 GCC 自举**,全链路实现。

> **Note:** 本项目迁移自教程操作系统 [Cinux](https://github.com/Charliechen114514/Cinux),已演进为完整的 13 Feature / ~50 Milestone 长弧。

</div>

---

## ✨ 项目简介

**Cinux** 是一个基于 x86_64 架构、采用现代 C++17 编写的操作系统。

> 💡 **为什么叫 Cinux?**
> - C/C++'s Linux, 也就是尝试重新再写一个基于 C/C++ 的 Linux
> - CharlieChen's *nux（逃）

13 个 Feature 域覆盖内核基础设施、内存管理、SMP 多核、设备驱动、VFS、网络协议栈、IPC、安全、用户态运行时、GUI 分离与 GCC 自举生态。**目标平台 QEMU / WSL2 KVM**。

---

## 🖼️ Screenshots

<p align="center">
  <img src="assets/README/static_gui.png" width="45%" alt="GUI Desktop">
  <img src="assets/README/static_cli.png" width="45%" alt="Shell">
</p>
<p align="center">
  <em>GUI 桌面环境（左） · CLI 终端环境（右）</em>
</p>

<p align="center">
  <img src="assets/README/run_example.gif" width="45%" alt="Boot to Shell">
  <img src="assets/README/multi_shell.gif" width="45%" alt="Multi Terminal">
</p>
<p align="center">
  <em>从启动到 Shell（左） · 多终端窗口（右）</em>
</p>

<p align="center">
  <img src="assets/README/parallel_work.gif" width="45%" alt="Parallel Work">
  <img src="assets/README/filesystem.gif" width="45%" alt="Filesystem">
</p>
<p align="center">
  <em>多终端并发执行（左） · Ext2 文件操作（右）</em>
</p>

> 📹 录像将随 v1.0.0 Release 更新(新增 SMP 双核 / TCP/IP ping / GCC 自举编译 demo),见 [assets/README/RECORDING.md](assets/README/RECORDING.md)。

---

## 🌟 特性亮点

<table>
<tr>
<td width="50%">

🧠 **完整 x86_64 内核**
Bootloader → Mini Kernel → Big Kernel → User Space → GUI,全链路打通;112 个系统调用,对齐 Linux ABI

</td>
<td width="50%">

⚡ **SMP 多核**
`-smp 2` 双核 online;per-CPU 架构 + IPI/trampoline + 多核调度 + lockdep 锁序图检测

</td>
</tr>
<tr>
<td>

📁 **多文件系统 + 三套存储**
ext2 读写 / ext4 只读 / tmpfs / ProcFS / DevFS;**NVMe + VirtIO-blk + AHCI** 三套块设备驱动;Dentry Cache + POSIX flock + 运行时 mount

</td>
<td>

🌐 **TCP/IP 协议栈**
以太网 / ARP / IPv4 / ICMP / UDP / TCP(握手/序号-ACK/挥手)+ Socket API;真 `ping 10.0.2.2`

</td>
</tr>
<tr>
<td>

🖥️ **GUI 用户态桌面**
[Cinux-GUI](https://github.com/Awesome-Embedded-Learning-Studio/Cinux-GUI) Compositor + Widget 树 + Terminal;**用户态进程**跑(`/dev/fb0` mmap + `/dev/input`),xHCI USB HID 键鼠

</td>
<td>

👨‍💻 **GCC 自举**
Cinux 上 `gcc hello.c && ./a.out`、`g++ hello.cpp`,cc1→as→ld 全闭环;默认 PIE;busybox init PID 1

</td>
</tr>
<tr>
<td>

🔒 **安全机制**
NXE + SMEP + SMAP(机制回读验证)+ ASLR + UID/GID + Stack Canary(-fstack-protector-strong)

</td>
<td>

🔧 **现代 C++17**
`constexpr` 编译期生成 GDT/IDT / RAII 锁 / `enum class` 驱动接口 / freestanding 零标准库 / `ErrorOr<T>` 无异常

</td>
</tr>
<tr>
<td colspan="2">

🔌 **musl 动态链接** — 移植 musl 作唯一 libc(不自建),kernel 做 PT_INTERP / interp 加载 / auxv,跑通 musl 动态 hello;双 libc 共存(musl 静态 busybox + glibc 动态 cc1)

</td>
</tr>
</table>

---

## 🏗️ 架构全链路

```
┌─────────────────────────────────────────────────────────────────┐
│  Boot: MBR → stage2(实模式→保护模式→长模式)→ Mini Kernel       │
│           ↓ 两阶段加载                                            │
│  Big Kernel(higher-half,-mcmodel=kernel)                         │
│   ├─ F1 类型库 + F2 mm(VMA/PageCache/Buddy/Slab)                 │
│   ├─ F3 进程线程 + F4 SMP(per-CPU/APIC/多核调度)                │
│   ├─ F5 驱动(AHCI/VirtIO/NVMe/xHCI/e1000/HPET)                  │
│   ├─ F6 VFS(ext2/ext4/tmpfs/ProcFS/DevFS)                        │
│   ├─ F7 网络(L2~L4 + Socket) + F8 IPC(Pipe/FIFO/Unix/shm/poll) │
│   ├─ F9 安全(SMEP/SMAP/ASLR/Canary)                             │
│   └─ F10 用户态(musl/ELF ldso/TTY/PTY)                          │
│           ↓ Ring 3                                                │
│  User Space: busybox + gcc/g++ + Cinux-GUI host(用户态进程)      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 🚀 快速开始

### 前置要求

本项目使用 CMake 构建目标(⚠️ 根目录无 Makefile,所有命令走 `cmake --build`)。需要:

```bash
# Ubuntu/Debian
sudo apt install -y gcc g++ binutils qemu-system-x86_64 cmake
```

> 支持最新的 g++ 15.2 编译(CMake 门禁要求 GCC >= 11)。

### 方式一:下载预编译镜像一键跑(v1.0.0 Release)

> v1.0.0 tag 推送后,GitHub Release 自动产出两个镜像 variant + 一键启动脚本。

```bash
# 1. 从 Release 下载(任选其一)
#    cinux-v1.0.0-console.ext2  — 精简 console(busybox/musl)
#    cinux-v1.0.0-desktop.ext2  — 完整桌面(含 gcc/g++ + cinux_gui_host)
#    run.sh                     — 一键启动脚本(内嵌 qemu 命令)

# 2. 跑起来
bash run.sh console   # 或 desktop
```

需要 VNC 客户端查看 GUI(desktop variant)。

### 方式二:源码构建

🚀🚀🚀 在 WSL 或者任何您喜欢的发行版中跑起来它们!🚀🚀🚀

> Feature Help: 不知道有没有好心人愿意移植到 Windows 上可编译,如果有所变动欢迎提交您的 PR!

#### Step 1️⃣: 配置

```bash
#  GUI(默认,Release 模式),也是最推介的!🚀
cmake -B build -DCMAKE_BUILD_TYPE=Release -S .

# 或者,默认(速度稍慢)
cmake -B build -S .

# 或者你 fork 改炸了准备使用 VSCode 调试
cmake -B build -DCMAKE_BUILD_TYPE=Debug -S .

# 带测试的配置
cmake -B build -DCINUX_BUILD_TESTS=ON -S .

# CLI 运行环境
cmake -B build -DCINUX_GUI=OFF -S .
```

#### Step 2️⃣: 构建
```bash
cmake --build build -j$(nproc)
```

#### Step 3️⃣: Cinux,启动!

```bash
cmake --build build --target run              # 跑内核本体,默认 VNC 显示,您需要 VNC!
cmake --build build --target test_host        # Host 端单元测试(CTest)
cmake --build build --target run-kernel-test  # QEMU 内核测试(自动退出)
```

> **🧪 验证内核改动用哪个?**
> - **`run-kernel-test`(首选)**:QEMU 内真实内核环境跑完整测试套件(ext2 读写 / syscall / fork-exec / 路径操作 / GUI 等),经 `isa-debug-exit` 自动退出并报告 pass/fail。**改动内核代码后用这个确认**。
> - **`run-kernel-test-all`**:一条命令顺序跑 **单核 → `-smp 2`** 两套(~1946 项),防"忘跑 SMP 变体",CI 用这个。
> - **`test_host`**:host 端 mock 测试,快但**不跑真实内核**,适合快速迭代逻辑。
> - **`run`**:交互式 GUI 预览(VNC),**不做自动断言**,仅用于人肉观察运行效果。

### 调试模式 1:GDB 大牛请走这里

```bash
# 终端 1:一键脚本构建并启动 QEMU 调试模式(Debug 构建 + GDB stub 监听 :1234)
bash scripts/launch_qemu_debug.sh

# 终端 2:连接 GDB
gdb build/kernel.elf
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
```

### 调试模式 2:VSCode 大牛请走这里(是的别坐牢,如果不喜欢 GDB!)

**Step 1:** 上面 `bash scripts/launch_qemu_debug.sh` 已起好 GDB stub。

**Step 2:** 确认 `.vscode/launch.json` 中已有如下配置:

> PS:大内核需要改一下 ELF,这个麻烦自己手调。
```json
{
    "name": "QEMU 调试 (mini kernel)",
    "type": "cppdbg",
    "request": "launch",
    "program": "${workspaceFolder}/build/kernel/mini/mini_kernel",
    "MIMode": "gdb",
    "miDebuggerServerAddress": "localhost:1234",
    ...
}
```

**Step 3:** 在 VSCode 中按 **F5**,选择对应的调试配置即可开始图形化断点调试。

---

## 🧪 测试与质量

- **`run-kernel-test-all`**:单核 + `-smp 2` 两 leg,~1946 项全绿(`isa-debug-exit` 自动退出)。
- **sanitizer 矩阵**:Debug/Release × UBSAN/LOCKDEP/ASAN/TSAN,CI 6 cell 全绿。
- **真 fork/CoW 压力**:`-smp` 跨核 forktest races=0。
- **机制回读**:启用硬件后写读寄存器验证真生效(SMEP/SMAP/CR4/EFER/LSTAR),不靠"没崩就算对"。
- **host ASAN 门禁**:CI 硬门禁,本地 `ctest` 默认不开,push 前自验。

详见 [CHANGELOG](document/changelogs/CHANGELOG.md) 的测试段。

---

## 🛠️ 技术栈亮点

<details>
<summary><b>🔍 现代 C++ 内核开发</b></summary>

- ✅ **C++17 特性**:`constexpr` / `if constexpr` / 结构化绑定
- ✅ **编译期魔法**:GDT/IDT 描述符 `constexpr` 生成,桌面图标 `constexpr` 像素数据
- ✅ **类型安全**:`enum class` 作为 API 一等公民,`NotNull<T>` 指针契约
- ✅ **RAII 资源管理**:Spinlock::guard、InterruptGuard、锁自动释放
- ✅ **零标准库依赖**:freestanding,自实现 memset/memcpy/string,错误经 `ErrorOr<T>` 传播(禁 throw/try/catch)
- ✅ **支持用户态/内核态 SSE**(故支持 -O2 Release 构建)

</details>

<details>
<summary><b>🧪 自研测试框架 + 双轨策略</b></summary>

```cpp
// 极简 API
TEST("测试名称") {
    ASSERT_EQ(actual, expected);
    ASSERT_TRUE(condition);
}

// 双轨测试策略
// Host 端:mock 硬件,验证逻辑正确性(快速迭代)
// Kernel 端:QEMU 运行,验证真实硬件行为(端到端)
```

外加 host 端 **TSAN/ASAN** 并发与内存安全检测,CI 矩阵 resident。

</details>

<details>
<summary><b>📖 文档与里程碑</b></summary>

- [ROADMAP](document/ai/ROADMAP.md) — 13 Feature / ~50 Milestone 长弧全树
- [CHANGELOG](document/changelogs/CHANGELOG.md) — v1.0.0 发版特性清单
- [document/notes/](document/notes/) — 每批工作记录(正式发布文档)
- [document/ci/](document/ci/) — 分支/提交/PR/发版工作流

</details>

---

## 🤝 参与贡献

欢迎贡献!你可以:

- 🐛 修复 Bug
- ✍️ 完善文档
- 💡 提出改进建议
- 📢 分享你的学习经验

详见 [document/ci/](document/ci/) 的工作流约定(分支策略 + 提交规范 + PR 流程)。

---

## 📄 许可证

本项目采用 [MIT License](LICENSE) 开源协议。

---

## 🙏 致谢

- [Cinux 教程项目](https://github.com/Charliechen114514/Cinux) - 这是本项目的起源!
- [OSDev Wiki](https://wiki.osdev.org/) - 宝贵的 OS 开发知识库
- [Writing an OS in Rust](https://os.phil-opp.com/) - 优秀的 OS 开发参考
- Linux 内核 - 永远的范式标杆
- musl libc / Buildroot / busybox / GCC toolchain - 用户态生态基石
- 所有为开源社区贡献的开发者

---

<div align="center">

**⭐ 如果这个项目对你有帮助,请给一个 Star!**

Made with ❤️ by [CharlieChen114514](https://github.com/Charliechen114514)

</div>
