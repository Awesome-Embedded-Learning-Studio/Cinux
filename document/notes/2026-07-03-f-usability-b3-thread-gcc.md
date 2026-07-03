# F-USABILITY: 批3 gcc driver 单命令冒烟闭环

> 2026-07-03, 分支 `feat/f-usability`。目标是让 Buildroot gcc profile 里
> `gcc -fno-pie -no-pie /hello.c -o /tmp/a.out && /tmp/a.out` 在 CinuxOS 上跑通。

## 结论

`cc1 -> as -> collect2 -> ld -> /tmp/a.out` 已经闭环。最终 gate 里生成的 glibc 动态
可执行文件在 CinuxOS 上输出:

```text
Hello from GCC!
[usability] PASS gcc-compile-run
[usability] result: PASS
```

这批不是单点缺 syscall, 而是 gcc driver 单命令把线程/进程/VMA/tmpfs/工具链闭包一起压上来。
几个关键根因:

1. gcc 用 `clone(CLONE_VM|CLONE_VFORK|SIGCHLD)` 起子进程; 父进程必须等 child `execve` 后再继续。
2. `mprotect()` 重插 VMA 时丢 file backing, VMA split/trim 又会丢右侧 file offset。
3. `open/openat(O_CREAT, mode)` 丢 create mode, tmpfs `stat()` 又硬编码 0644, 导致 ld 生成的
   `/tmp/a.out` 可读但不可执行。
4. `waitpid(-1, ...)` 成功时 syscall 返回了 selector `-1`, 而不是实际 reaped pid; BusyBox ash
   因此把退出 0 的 `/tmp/a.out` 看成失败。
5. host Arch GCC specs 会传 `-lgcc_s_asneeded` / `-latomic_asneeded` 和 `liblto_plugin.so`;
   gcc rootfs 需要把这些 linker scripts/plugin 一起 staged。

## 改动

### 1. forced SIGSEGV

用户态不可修复 page fault 改为强制投递 SIGSEGV: 即便进程把 SIGSEGV block 或设成 ignore,
内核也会清掉 block/ignore, 让同步 fault 不再被吞掉。机制测覆盖 block+ignore 旁路。

### 2. 最小 CLONE_VFORK

`clone()` 支持 `CLONE_VFORK`: child 记录 `vfork_parent`, parent 在 child 入队前进入 blocked,
child 成功 `execve()` 后唤醒 parent。`execve()` 对 vfork child 会先从共享 parent
AddressSpace detach, 再清空旧 VMA/页表, 避免 child exec 时拆掉 parent 的地址空间。

### 3. tmpfs/open 对齐 gcc 输出文件

- `sys_open()` 走 `do_openat_kernel()`, 老 open(2) 也支持 `O_CREAT/O_TRUNC`。
- tmpfs regular file 实现 `truncate()`。
- `open/openat(O_CREAT, mode)` 传递 create mode, 并应用 task umask。
- tmpfs `stat()` 使用 inode mode, `chmod()` 可更新 regular/dir mode。

这让 ld 生成的 `/tmp/a.out` 带 execute bit, BusyBox ash 的 `-x`/exec 检查能通过。

### 4. file-backed VMA 保护 backing/offset

`sys_mprotect()` remove+insert 时保留 file backing 引用和调整后的 file offset。`VMAStore::remove()`
在右侧 survivor 上递增 file offset; `insert()` 不再按 flags 把 file-backed VMA 草率 merge 到一起。
这避免 glibc/ld 映射段 offset 串线或 backing 丢失后读到零页。

### 5. waitpid(-1) 返回实际 child pid

`proc::waitpid()` 增加可选 `reaped_pid` out 参数; `sys_waitpid()` 成功时返回实际 reaped pid。
之前 `pid == -1` 成功也返回 `-1`, shell 把 syscall 看成失败, 即使 status 里是 exit 0。

### 6. gcc rootfs 闭包补齐

`tools/gcc-toolchain/extract.sh` 继续保持 host-copy 策略, 本批新增:

- `/usr/bin/gcc`
- `collect2`
- `liblto_plugin.so`
- `libgcc_s.so`, `libgcc_s_asneeded.so`
- `libatomic.so*`, `libatomic.a`, `libatomic_asneeded.so`

## 验证

- `timeout 180 cmake --build build-bu --target run-buildroot-usability -j$(nproc)`
  - QEMU target 成功退出。
  - 串口出现 `Hello from GCC!`。
  - `[usability] PASS gcc-compile-run`
  - `[usability] result: PASS`
- `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)`
  - single + `-smp 2` 两腿均 `ALL TESTS PASSED`。
  - 最终 `1108 passed, 0 failed`。

## GOTCHA

1. **vfork 不是普通 fork**: `CLONE_VM|CLONE_VFORK` 下 parent 继续跑会和 child exec 共享同一个
   AddressSpace, 轻则 child stack 被 parent 改, 重则 child 清 VMA 时拆 parent。
2. **`waitpid(-1)` 成功不能返回 -1**: 返回值是 child pid, selector 只是输入条件。这个 bug 会让
   shell 把 exit 0 看成命令失败。
3. **可执行文件 mode 来自 open create mode**: ld 生成 `/tmp/a.out` 时依赖 `O_CREAT` 的 mode
   语义; tmpfs stat 不能永远报 0644。
4. **file-backed VMA 不能按 flags 合并**: ELF/ldso 相邻段 flags 可能一样, 但 backing offset 不同;
   只按 flags merge 会把后续 fault 引到错文件 offset。
5. **Arch GCC 默认 specs 不是“只要 libgcc.a”**: `-fuse-linker-plugin` 和 as-needed linker script
   都会参与最小 C 程序链接。

## follow-up

- `prlimit64`/`getrandom`/`rseq` 等 glibc probe 目前仍走 `-ENOSYS` fallback; 可按日志降噪或补 stub。
- `gcc-smoke` CI job 还需要把 gcc profile rootfs assemble/pack 流程固化成 CMake/CI target。
- 批4 g++ 需要继续扩 `extract.sh`: `cc1plus`、`libstdc++`、C++ header closure。
