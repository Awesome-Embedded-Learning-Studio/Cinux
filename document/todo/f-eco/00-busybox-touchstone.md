# F-ECO 第一关:busybox CI 试金石

> 用 busybox(C,musl 静态)当 CI 试金石,普及 syscall,持续声明「Cinux 能跑 core utilities」。
> 范围:功能 + 并发稳定性(-smp)。时间性 soak / 资源边界 不在本关(见 [README](README.md) 稳定性三划分)。

## 为什么 busybox 当第一关(不是 CFBox)

- **C 干净**:musl 直接编,CinuxOS 已跑通 musl C(hello / hello-dyn),零新变量。
- **信号纯**:崩了基本就是 syscall 缺口,定位干净(不像 CFBox 叠加 C++ 运行时未知)。
- **syscall 复用**:busybox 补的 syscall,CFBox 直接白捡(CFBox 是 busybox 替代,applet 同构)。
- **梯度不丢**:busybox `menuconfig` 照样选 applet 子集(minimal → full)。

## 8 批排序(以 syscall 普及为第一轴,busybox applet 当验收刻度)

| 批 | busybox 验收 applet | 新增 syscall | F 归属 | 量 |
|----|---------------------|--------------|--------|-----|
| 0 | echo/cat/ls 裸跑摸底 | —(验证 musl C 能跑,拿第一个 crash) | — | 小 |
| 1 | `ls` | `getdents64` (217) | F6 | 小 |
| 2 | `cp/mv/rm/touch/ln/mkdir/chmod/chown` | `rename`/`symlink`/`link`/`readlink`/`utimensat`/`chmod`/`chown`/`umask`(8) | F6 | 中 |
| 3 | `cat/head/tail/wc/sort/grep/sed`(负载) | `nanosleep`(sleep) | — | 小 |
| 4 | `sh` + 管道 + 重定向 | `dup`/`dup2`/`fcntl` 子集(质变点) | F10-M3 followup | 中–大 |
| 5 | `ps/free/top/kill/uptime` | `sysinfo`/`getrusage` + /proc 扩字段 | F6-M2 扩展 | 中 |
| 6 | `mount/init` PID 1 | `mount`/`umount2` + tmpfs | F6-M4 + F6-M1 T5 | 中 |
| 7 | `ifconfig/ping/wget/nc` | ~11 socket(`socket`/`bind`/`connect`/`listen`/`accept`/`sendto`/`recvfrom`/`setsockopt`/`getsockname`/`getpeername`/`shutdown`) | F7-M6(协同网络 workflow) | 大 |
| 8 | `id/whoami/login/su` | `getgroups`/`setgroups` + 权限 enforcement | F9 | 小–中 |

**syscall 累计**:54 → **89**(已注册,目标 100+ 近在咫尺)。

**进度**:批0–8 ✅ + busybox 14/14 真验收 + PTY 终端交互(GUI)。两 leg 1062/0 + host 69/69。

**远期路线(用户决策:砍 Lua/TinyCC 自建,GCC 自举)**:
- **层3 init/login**(批6 mount/init PID1):从"手动 execve shell"到"系统自动启动"。
- **层4 GCC 自举**(F12-M2):GCC+binutils 在 CinuxOS 上跑 → `gcc hello.c -o hello && ./hello` → 从"能跑别人的二进制"到"能编自己的二进制"。**最不可控里程碑**。做法:纯 musl 编 GCC 或 glibc GCC。
- **层5 UEFI**(F11):BIOS→UEFI 双启动。
- **层6 近日常使用**:GUI+稳定+GCC 自举闭环 → **在 CinuxOS 上编 CinuxOS**。

**节奏**:批 0 摸底先行 → 批 1/2 低垂果 → 批 4(sh)硬骨头 → 批 5/6 内核工作多 → 批 7 蹭网络 workflow。

## CI 试金石形态

新 target(类比 `run-kernel-test-all`),四层:

- **smoke**:5 个最快(echo/cat/ls/true/exit),快速断 ABI 没碎。
- **functional**:cp/mv/ln/grep 真比对(四件套)。
- **negative**:坏输入验 errno。
- **stress**:大文件 / SMP 并发 / 循环 N 次。
- **-smp 下也跑**(两 leg 教训,别再空转假绿)。
- 每 push 跑,退化即红——持续门禁,不是一次性 demo。

## 用例标准(四件套 + 必带负用例)

- 每用例:**输入 + 期望输出(精确比对) + 退出码 + 副作用**。
- 必带负用例:坏输入 → 特定 errno。
- **铁律:退出码 0 不算过,输出 + 副作用精确匹配才算过**(防 getdents64 失败 ls 列空那种假绿)。

## 批 0 动作(摸底)

1. 获取 busybox 源码(`menuconfig` 最小:echo/cat/ls)。
2. musl 静态编译(CinuxOS sysroot),产单二进制。
3. 塞 ext2 image + QEMU 裸跑,拿第一个 crash 信号。
4. 立 echo/cat/ls 四件套用例 + ≥1 负用例(试金石第一批种子)。

**批 0 双重身份**:既摸底(暴露真实缺口),又是试金石第一块砖(用例立起来)。批 0 是纯 host 工具链 + image 侧,**不动内核代码、不碰网络/GUI**,完全独立。
