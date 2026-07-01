# F-ECO 批8:getgroups + setgroups(补充组)

> 2026-07-01。外包 worktree `feat/outsource-f-eco-b8-groups`(从集成线 `9208751`),cherry-pick 回 `feat/f-eco-b2-vfs-syscalls`(`dc3fdc1`)零冲突。两 leg **1062/0**(1059+3)+ host 69/69。**未 push**。
> busybox 试金石第八刀**小件**:`id`/`newgrp` 的补充组(getgroups/setgroups)。login/su 复杂件留后续。

## 实现

- **Task +groups 存储**([process.hpp](../../kernel/proc/process.hpp)):`groups[NGROUPS_MAX=32]` + `ngroups`。放 **fpu_state 之后**(fpu_state 的 offset 384 被 static_assert 钉死——syscall/interrupt FXSAVE 硬编码偏移;插前面会推后它 → 编译红)。fork/clone 经 whole-Task memcpy 继承(同 uid/gid/umask)。
- **sys_getgroups(115)/setgroups(116)** + `do_getgroups_kernel`/`do_setgroups_kernel`(放 [sys_creds](../../kernel/syscall/sys_creds.cpp) 家族)。
  - **do_ 取 `Task*` 参数**(关键设计):测试内核 boot 线程 `Scheduler::current()==null`(F3-M4 GOTCHA#22:main_test 在 boot 栈直跑不进 registry),故 do_ 解耦 scheduler 取 Task*,sys_ 解析 current 后传入。getgroups:`size==0`→返 count;`size<count`→EINVAL;填 user gid_t 数组。setgroups:root-only(`euid==0`,CAP_SETGID 简化),`count>NGROUPS_MAX`→EINVAL;sys_ 用 kernel 暂存 buffer 过 copy_to/from_user。
- syscall_nums +SYS_getgroups=115/setgroups=116;dispatch 注册(sys_creds.cpp 已在 CMake,无改)。

## 机制测试(防假绿,3 测,栈 Task 直驱 do_)

- getgroups count/array:do_setgroups 3 组 → do_getgroups count==3 + 数组精确匹配 + buffer-too-small→EINVAL。
- setgroups too-many:count=99 → EINVAL(>NGROUPS_MAX 32)。
- **setgroups 非 root→EPERM**:栈 Task `euid=1` → do_setgroups → EPERM。

## GOTCHA

- **fpu_state offset 384 钉死**:Task 加成员须放 fpu_state **之后**(或更新 asm 偏移)。第一版 groups 插在 creds 块(egid 后,fpu_state 前)→ 推后 fpu_state → static_assert 红。改插 fpu_state 后解决。
- **测试内核 current()==null**:creds 类 handler 在测试内核里 `Scheduler::current()` 返 null(sys_creds 既有 `task==nullptr ? 0 : task->uid` 即为此)。故凡需操作 current task 状态的 do_ 变体要取 `Task*` 参数(测试传栈 Task),不能依赖 current()。

## follow-up(留后续)

- **login/su**:getgroups/setgroups 是基础;真 login(/etc/passwd 解析 + setuid/setgroups 切身份)+ su(set-user-id binary)是复杂件,另立弧。
- **setgroups 权限**:现 root-only(euid==0);真 CAP_SETGID 能力位 + capability 框架留 F9 续。
- **groups 用于权限判定**:文件 group 权限位检查应查 supplementary groups(现 F6 check_permission 只查单 gid;扩 groups 留 F6)。
- busybox `id` applet 端到端验收:留 CI build。

## 验证

`run-kernel-test-all` 两 leg:单核 1062/0 → -smp 2 1062/0 → ALL TESTS PASSED(基线 1059 + 3 getgroups/setgroups)。`test_host` 69/69。

**push/PR 归用户**——F-ECO 外包线,等回主线。
