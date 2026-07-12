# F12-M2 — GCC 自举（glibc 动态工具链）

> 批4。从「能跑别人二进制」到「能编自己二进制」。终极目标 `gcc hello.c -o hello && ./hello` 在 Cinux 跑通,且编出来的 hello 能直接跑（自举闭环）。
> 详尽批级切片见 plan `~/.claude/plans/parsed-zooming-fog.md`。

## 用户决策（2026-07-02,校正旧 musl 静态自编方向）
- **动态 glibc**(非 musl):兼容最广,对齐真实发行版。杠杆在 PT_INTERP 是 libc 切换缝 —— kernel 只认 Linux ABI,换 ldso+libc.so 即切 libc,F10-M2 动态加载机制复用。
- **不自编工具链**:直接从本机系统(gcc 16.1.1 + binutils 2.46,glibc 动态)提取现成 gcc/cc1/as/ld + glibc 运行时 + crt + headers 装盘。
- **先 as+ld 链路再 cc1**:cc1(47MB 大 ELF + 大 mmap)最不可控,隔离到 B4-b。第一刀先把 glibc 动态 ELF 加载链路验通。
- **musl/glibc 共存(首选)**:进程间共存零风险(内核中立),保 busybox -O2 musl ABI 试金石;回退条件=glibc 链路死活验不通,届时全栈 glibc 重编 busybox。

## B4-a 切片（第一刀 = as+ld 链路）
1. **B0 内核 glibc 兼容 4 处 ✅** — auxv +AT_PLATFORM/AT_HWCAP + execve 扫 PT_GNU_STACK + brk 64MB→240MB。
2. **B1 host 提取 + 装盘 ✅** — `tools/gcc-toolchain/extract.sh`(as/ld 子集,不含 cc1/headers)+ link-time libc artifacts + `create_ext2_disk.sh` 升级 `mkfs.ext2 -d` 目录树 + 扩盘 128MB + inode 8192。
3. **B2 as 冒烟 ✅** — `as --version` 验 glibc ldso 加载 + `as hello.s -o hello.o`;按实际日志补 pread64/uaccess/page-cache 缺口。
4. **B3 ld 链路 + 自举 ✅** — `ld hello.o + crt + libc + libgcc -o hello && ./hello`(glibc 动态 ELF 跑 printf "Hello from GCC!")。

## 后续（outline,本弧不做）
- **B4-b cc1**:装 headers(249MB)+ cc1(47MB);`gcc -c hello.c`(大 ELF + 大 mmap + mremap/getrusage 缺口)。
- **B4-c 完整自举**:`gcc hello.c -o hello && ./hello`(gcc driver fork cc1→as→ld 管道/临时文件链全打通)。

## follow-up（显式不做）
- brk 动态增长 / 动态 ldso 基址(堆 >240MB 时;cc1 实测堆用量后定)
- vDSO(AT_SYSINFO_EHDR;glibc fallback syscall,慢不死)
- AT_HWCAP2(扩展特性 AVX 等;CPUID.07H)
- 全栈 glibc(仅当混搭真不可行)
