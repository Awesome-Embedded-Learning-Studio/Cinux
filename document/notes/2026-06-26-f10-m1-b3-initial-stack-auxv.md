# F10-M1 批3 — execve 压 Linux 初始栈（argc/argv/envp/auxv）

> 里程碑：F10-M1 用户态运行时 / musl 静态移植（批3）。分支 `feat/f10-musl`，commit `20baf76`。
> 验证：`build_initial_stack` host 单测 3/3 + 全量编译绿 + `run-kernel-test` 945/0 + `make run` 无崩。

## 背景与方法

musl 的 `__init_libc` / `__libc_start_main` 启动时从用户栈读辅助向量（auxv）——`AT_PHDR`/
`AT_PHNUM`/`AT_PHENT` 找程序头（PT_TLS 等），`AT_PAGESZ`（断言非 0），`AT_RANDOM`（SSP canary），
`AT_UID/EUID/GID/EGID` + `AT_SECURE`，`AT_ENTRY`。原 CinuxOS `execve`/`launch_user_program` **完全不铺
用户栈内容**（`argv`/`envp` 被 `(void)` 忽略，`ctx.rsp` 指向 demand-fault 出的零页）→ musl 读
`argc=[rsp]=0`、auxv 全无，直接起不来。

照批2 方法论（读 musl 源码 `src/env/__libc_start_main.c` + Linux UAPI，不猜）确定 musl 硬依赖的 AT_*
键，再实现。

## recon 关键结论（影响打法）

- **execve 成功路径在 run-kernel-test 里零覆盖**：`test_fork_exec` 只测常量/枚举/header 校验/失败路径
  （`sys_execve("/bin/test")` 断言返 `<0`）。成功路径要 VFS+真 ELF，harness 没有 → **没回归网**。
  → 这就是为什么**抽纯函数 helper 做测试缝**：把能测的栈布局逻辑剥出来单测，绕过"成功路径不可测"。
- **栈是两条路**：`launch_user_program`（首进程：execve 后自己 map 栈页+跳 entry）和 `execve`（替换
  路径：完全不铺栈）。批3 接 `launch_user_program`（批6 用它 boot musl hello world）；`sys_execve`
  替换路径的栈铺设施 follow-up（当前无调用方）。

## 改动

### 1. 纯函数 helper `build_initial_stack`（kernel/proc/initial_stack.hpp）

右对齐铺 Linux x86_64 初始栈进 caller buffer（`buf[cap-1]` ↔ 用户 VA `stack_top-1`），这样 caller
能把栈顶页的 direct-map 内核虚址直接当 buf 传，**零拷贝**。布局（低 VA→高 VA）：
```
[ argc | argv[] | NULL | envp[] | NULL | auxv.. | AT_NULL | strings.. | pad ]
```
- argv/envp 指针、AT_RANDOM/AT_EXECFN 值为**绝对用户 VA**（从 `stack_top` 推：`va = stack_top - cap + off`）。
- 入口 RSP = `stack_top - size`，**16 字节对齐**（SysV 进程入口约定；size 向上取整到 16 的倍数）。
- AT_RANDOM/AT_EXECFN/AT_NULL 由 helper 注入（值依赖它自己算的字符串布局）；其余 AT_* caller 传。
- header-only、kernel+host 双编译、自带 `strlen`/`memcpy`（freestanding kernel 无 `<cstring>`）。

### 2. execve 加 `ElfAuxInfo` out-param

`execve(path, argv, envp, ElfAuxInfo* aux_out = nullptr)`。成功时填 `at_phdr`（PT_LOAD 循环里按
`p_vaddr + (e_phoff - p_offset)` 算——覆盖 phdr 表的段）、`at_phnum`、`at_phent=56`、`at_entry`。
`sys_execve` 调用传 nullptr（行为不变，follow-up 再接）。

### 3. `launch_user_program` 接管栈铺设

execve 后 map 栈页、**捕获栈顶页 phys**、build_initial_stack 直接写栈顶页（direct-map 零拷贝）、
`jump_to_usermode(entry, stack_top - size, 0)`。AT 列表含 musl 必需：PHDR/PHNUM/PHENT/PAGESZ(4096)/
ENTRY/UID/EUID/GID/EGID/SECURE + AT_RANDOM（16 字节，`g_random.next64()` 两次）。原
`USER_ABI_RSP_OFFSET=8`（`rsp%16==8`，不合 SysV）换掉，现 `rsp%16==0`。

## 验证

- **helper host 单测 3/3**（`test/unit/test_initial_stack.cpp`，host `ctest`）：按"用户进程在 `_start`
  读回"的方式遍历——argc 值、argv/envp 指针解析到正确串、auxv 的 AT_PHDR/PAGESZ/ENTRY/UID 值对、
  helper 注入的 AT_RANDOM 指向 16 字节、AT_EXECFN 指向 filename、AT_NULL 终止、RSP 16 对齐、溢出返 0。
- 全量编译绿（含 host test_initial_stack + user_shell 重编）。
- `run-kernel-test` 945/0（execve 成功路径本无覆盖，失败路径不变）。
- `make run` boot 无崩（ext2 挂载→GUI desktop→xHCI→idle，无 panic/#UD/#GP/#PF）。

**诚实边界**：`launch_user_program` 只在 GUI 点 Shell 图标时调（gui_init.cpp shell_child_entry），
`make run` 自动冒烟**不经过它** → end-to-end（真程序在新栈上跑）留**批6 musl hello world** 验证。
批3 的机制正确性由 host 单测的 helper 兜（launch 只是调这个测过的 helper + 简单 plumbing）。

## 陷阱

- **`USER_ABI_RSP_OFFSET=8` 不合 SysV**：原 launch 入口 rsp = stack_top-8，`rsp%16==8`（函数入口约定），
  但**进程入口**该 `rsp%16==0`（无返回地址）。helper 产出 size%16==0 修正之。现 CinuxOS 进程入口
  合规，musl crt 的 `and $-16,%rsp` 也兼容。
- **`cd /tmp` 持久 cwd**（批2 同坑）：clone musl 的 `cd /tmp` 切走 Bash 工作目录，后续相对路径 grep
  空。用绝对路径修。
- **make run 不点图标不起 shell**：冒烟看不到 `[EXECVE] loaded`/`jumping to user mode` 是正常的
  （GUI 等交互），不是 launch 坏了。判断 launch 坏没坏看有没有 panic/fault（没有 = OK）。
- **VNC 5900 偶发占用 + timeout 40 偶尔不够**（批2 同坑）：retry / 给 90s。

## 下一步

批4：补 musl 实际会发但 CinuxOS 缺的 syscall——`openat`(257)/`newfstatat`(262)/`exit_group`(231)/
`wait4`(247)（musl 的 open/stat/exit/waitpid 走它们，非老的 open=2/stat=4/exit=60/waitpid=61）。照
批1/批2 方法：对照 musl `arch/x86_64/bits/syscall.h.in` + wrappers 看真正调用集，每个加最小测试。
