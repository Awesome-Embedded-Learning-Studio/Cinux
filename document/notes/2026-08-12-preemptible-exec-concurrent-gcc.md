# 可抢占 exec 与 GUI 双 GCC 并发收敛

> 日期：2026-08-12 · 分支 `feat/nvme-async-io` · 状态：✅ 独立单核/SMP 门禁闭环，GUI 人工复验待用户执行

## 现场

两个 GUI shell 交错运行 `gcc hello.c -o hello1` 与 `gcc hello.c -o hello2` 时，一路曾无结果；
随后在另一 shell 执行 `ls`，系统静默。新一轮串口给出了决定性故障：

```text
segfault: ... rip=0xFFFFFFFF81009486 ... err=0x15
```

当前 `big_kernel` 中该地址精确落在 `context_switch.S` 的 `.restore: ret`。fault 使用用户栈，
`err=0x15` 表示用户态 instruction fetch 访问 supervisor 页，因此不是随机用户 ELF 入口或普通
缺页，而是内核返回点被当成用户 RIP。

日志里的 `/bin/as`、`/sbin/as` lookup failure 是 GCC 按 PATH 探测 assembler 的正常过程；随后
会命中实际存在的 `/usr/bin/as`，不构成失败证据。exit 日志仍显示 `kernel_init` 是因为 fork
继承 Task name，exec 当前不更新该诊断字段，也不能用它判断实际 executable。

## 根因一：把用户入口暂存在 scheduler context

旧 `execve()` 在完成 ELF 装载后把入口写入 `task->ctx.rip`，稍后
`enter_loaded_program()` 再从同一字段取出入口。任意 timer/IRQ-exit 抢占都可能落在两者之间；
`context_switch` 保存 outgoing task 时会合法地把 `ctx.rip` 覆盖为自己的 `.restore` continuation。
任务恢复后，exec 路径便把该内核 continuation 交给 `jump_to_usermode()`，与现场固定地址、用户
RSP 和 fault error code 完整吻合。并发 GCC 只是显著放大了这个窗口。

修复让 `ElfAuxInfo::initial_rip` 独立携带真正的初始用户 RIP，`execve()` 通过必填引用返回它；
launch 路径不再读取 scheduler-owned `Task::ctx.rip`。动态 ELF 仍严格区分：
`initial_rip` 是 interpreter 入口，`AT_ENTRY`/`at_entry` 是主程序入口。

## 根因二：waitpid 的 scan→sleep 丢唤醒

旧 waitpid 先扫描 children，未见 Zombie 后才 `prepare_to_wait()`。如果子任务恰在两步之间退出，
子侧 generic unblock 会因为父仍是 Running 而无操作；父随后变 Blocked 并 schedule，之后再无人
唤醒。GCC driver、cc1、assembler、linker 与 `ls` 都会快速 fork/exit，这正是双核下高概率命中
而单核难复现的静默挂起。

现在 `prepare_child_wait()` 与 `publish_child_exit()` 共用一个 IRQ-safe child-exit lock：父侧在
同一临界区完成条件扫描、精确 wait-reason 标记和 Blocked 发布；子侧在同锁内发布 Zombie，且
只唤醒真正的 waitpid waiter。不能改成无条件唤醒——父可能正等 futex、vfork、pipe 或 IO，
generic wake 会违反那些 wait queue 的 grant 协议。退出路径另用 `IrqSaveGuard` 保护
Zombie publish→parent/vfork wake→最终切走，避免本地 timer 抢在通知完成前带走 Zombie task。

同批把 PID 释放推迟到 target 已从 CPU quiesce、registry 注销和对象回收之后；超时泄漏分支
保留 PID，避免旧 Task 尚可见时把相同 pid 分给新任务。

## 双 GCC 需要的文件系统并发边界

现场并不能单独证明每一条文件系统风险都已发生，但双编译已把这些 production 路径放进同一
验收，因此不能只修 exec/waitpid 后继续赌时序：

- Ext2 的目录块与 inode-table block 都是 read-modify-write。两个输出文件即使名字和 inode
  不同，也可能修改同一物理块并产生 lost update。现在用一个 sleepable FS-wide
  `mutation_lock_` 覆盖 create/mkdir/link/rename/unlink、write/truncate 与 metadata mutation；
  read/stat 同步 cached disk inode，stale refresh 只更新可变 VFS metadata，绝不重写
  `ino/type/ops/fs_private`。
- GCC/collect2 会在 `/tmp` 创建并 unlink 临时文件。TmpFs 现在分别持 namespace ref 与 caller/open
  ref；unlink 只摘 namespace，最后一个 fd/ref 的 release 才删除节点，因此旧 fd 可在 unlink 后
  继续安全读写，同名 recreate 与旧对象隔离。
- `openat(O_CREAT|O_EXCL)` 现在对已存在或并发创建冲突返回 `EEXIST`，非 exclusive lost-create
  会重新 lookup；全局 KRandom 状态由 IRQ-safe lock 串行化，避免并发 `getrandom()` 丢状态更新。

这是优先保证正确性的粗粒度边界；Ext2 后续可再拆 per-directory、per-inode 与 inode-table 锁，
但不能退回无事务保护的块级 RMW。

## 门禁

- 独立 `/tmp` build、`-display none` 单核：core `950/950`，连同 VirtIO `953/953`。
- 同一 build 的 SMP：core `950/950`，连同 VirtIO `953/953`；AP wake/readback 与 TLB shootdown PASS。
- Ext2 host TSAN：4/4；含双线程 create+write+chmod、销毁/remount 后 payload/mode 精确回读，以及
  stale refresh 与 read/stat 交错；连续 20 轮无 race。
- 定向机制测覆盖 exec entry 与 scheduler context 独立、waitpid 两种发布顺序、旧 scan→prepare
  窗口、unrelated Blocked parent 不得被 child exit 唤醒、TmpFs open→unlink→recreate、O_EXCL。
- 所有 QEMU 验证均使用独立镜像；未启动、输入或修改用户正在操作的 GUI/QEMU。

## 留续与人工验收

- `Task::parent` 仍是裸指针，父先 deferred-free、孤儿后退出存在 DEBT-024；需单独设计
  reparent-to-init 或受保护的 parent lifetime。
- NVMe async wait 仍缺设备 timeout/reset，CID generation/多 in-flight 也仍是后续。
- 最终 GUI 验收由用户在 fresh rebuild/rootfs 上同时生成两个不同输出名，分别执行，再立即 `ls -l`
  确认两者存在；在用户确认前，不把真实双 GUI GCC 宣称为已通过。
