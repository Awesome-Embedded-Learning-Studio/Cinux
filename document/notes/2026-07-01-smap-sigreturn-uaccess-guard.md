# SMAP/sigreturn 修复:BusyBox SIGCHLD 返回路径裸读用户栈

**日期**:2026-07-01
**分支**:`feat/f-eco-b2-vfs-syscalls`
**commit**:`1c1d9ad`
**验证**:`run-kernel-test-all` 两 leg **1061/0** + BusyBox smoke **14/14 PASS**(两 leg) + `check_uaccess_boundaries`

## 背景

GUI terminal 已经从 pipe 切到 PTY,并修了 PTY slave 空读阻塞后,busybox `sh` 能留在交互态。
随后在 GUI shell 里执行 `ls` 时出现内核态 page fault:

- `/bin/ls` exec 成功,子进程 `sys_exit(0)`;
- parent shell `waitpid` reap child;
- shell 收到 `SIGCHLD`,handler 返回进入 sigreturn trampoline;
- `sigreturn_handler` 直接用 `frame->rsp` 裸读用户栈上的 `SignalFrame`;
- SMAP 下 AC=0,kernel 裸读 user page → kernel-mode #PF。

这不是 `/bin/ls` 崩,而是 `ls` 正常退出后,shell 的 **SIGCHLD handler 返回路径** 崩。

## 修复

- [signal.cpp](../../kernel/proc/signal.cpp)
  - `signal_setup_frame` 不再手写 `stac/clac` + raw store,改先组装 kernel-local `SignalFrame`,再 `copy_to_user` 写用户栈。
  - `sigreturn_handler` 不再 `reinterpret_cast<SignalFrame*>(frame->rsp)` 后直接解引用,改 `copy_from_user` 读入 kernel-local frame。
  - 提取 `signal_restore_frame()` 纯 helper,测试只测纯寄存器恢复逻辑,user-memory boundary 留给 sigreturn handler。
- 同批清掉周边同类风险:
  - `clone` 的 `CLONE_PARENT_SETTID` / `CLONE_CHILD_SETTID` 用 `put_user`;
  - `clear_child_tid` 用 `put_user`,失败只记录并跳过 wake,避免 exit 路径 panic;
  - `futex_wait` 读 futex word 用 `get_user`;
  - `arch_prctl(ARCH_GET_FS)` 用 `copy_to_user`;
  - `sys_dmesg` 先格式化到 kernel buffer,再 `copy_to_user`。
- 测试改用 [UserPage](../../kernel/test/user_page.hpp):机制测里显式映射 user page,避免继续用 kernel 栈地址制造 SMAP 假绿。

## 防回归

新增 [scripts/check_uaccess_boundaries.sh](../../scripts/check_uaccess_boundaries.sh),并挂到所有 `run-kernel-test*` target:

- 禁 `kernel/proc` / `kernel/syscall` / `kernel/drivers` 里手写 `cinux::arch::stac/clac`;
- 禁已知 user-pointer 名称直接 `*reinterpret_cast<T*>(...)`;
- 对 `kernel/syscall` 再加一层较宽的参数名规则(`*_virt` / `*_ptr` / `uaddr` / `buf` / `addr` 等),挡住新 syscall 换变量名绕过。

做过临时负测试:加一个违规 syscall 文件:

```cpp
cinux::arch::stac();
*reinterpret_cast<uint8_t*>(buf_virt) = 0;
cinux::arch::clac();
```

脚本按预期失败,同时命中 manual `stac/clac` 与 raw deref 两类规则。临时文件已删除,工作树恢复 clean。

## GOTCHA

- **run-kernel-test 只有 kernel 地址会假绿**:SMAP 只拦 supervisor 访问 user page;测试若继续用 kernel 栈模拟 user pointer,裸解引用不会 fault。user-boundary 测试要用真 user-mapped page 或 ring-3 smoke。
- **sigreturn 是 syscall 以外的 user-memory boundary**:它不是普通 `sys_*` 文件,但同样从用户栈取 payload。凡看到 `frame->rsp` / signal frame / trampoline 返回路径,都必须按 user pointer 处理。
- **extable accessor 只能保护 accessor 内部**:裸解引用不会自动变成 `-EFAULT`,仍然是 kernel #PF。防线必须是代码审查 + 静态检查 + 机制测三层。

## 验证

最终版:

- `scripts/check_uaccess_boundaries.sh` → `[uaccess] boundary check passed`
- `timeout 200 cmake --build build --target run-kernel-test-all -j$(nproc)`
  - single CPU: **1061 passed,0 failed**, BusyBox smoke **14/14 PASS**
  - `-smp 2`: **1061 passed,0 failed**, BusyBox smoke **14/14 PASS**

## follow-up

- 静态脚本是保守 grep guard,不是完整 C++ 语义分析。后续若误报或漏报,优先维护脚本规则,不要回退到人工记忆。
- GUI 交互仍需单独观察:PTY 路径已能稳定 IO,但回显即时性与 terminal poll 策略属于 GUI terminal 层问题。
