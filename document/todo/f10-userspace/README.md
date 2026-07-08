# F10: 用户态运行时

> 完善用户态生态:libc 扩展、动态链接器、TTY 子系统、PTY 真终端会话。

> **✅ F10 全里程碑收官**（2026-07-08 核对）：M1 musl 静态(PR#42)+ M2 ELF 动态(PR#49)+ M3 TTY+PTY(PR#46+#50)+ M4 PTY 真终端(PR#72)全合 main。原 M5(musl+glibc 兼容)已并入 M1;glibc 动态兼容在 F12-M2 `c7c1ff5` 落地。下方"全部四项"为原始立项文案,M1 实际走 musl-first(见 [00-libc.md](00-libc.md))。

## 实现决策

全部四项:
1. libc 扩展(21→80 syscall)
2. ELF 动态链接器
3. TTY 子系统(伪终端 + 行规范)
4. PTY 真终端会话(fork+execve-under-PTY;busybox sh 作消费者)

> init 用 busybox init(批3b ✅)。CinuxOS 用现成 busybox/gcc,不自建 userland。

## Milestone 依赖

```
M1 libc 扩展 ──→ M2 ELF 动态链接器
       ↓                ↓
M3 TTY 子系统    M4 PTY 真终端会话
                        ↓
                 M5 musl libc + glibc 兼容验证
```

M1 是所有其他的前置。M3 可与 M2 并行。

## 文件清单

| 文件 | Milestone |
|------|-----------|
| [00-libc.md](00-libc.md) | M1: musl 静态移植(musl-first,✅ PR#42) |
| [01-elf-dynamic.md](01-elf-dynamic.md) | M2: ELF 动态链接器 |
| [02-tty.md](02-tty.md) | M3: TTY 子系统 |
| [04-musl-glibc.md](04-musl-glibc.md) | M5: musl libc + glibc 兼容 |
