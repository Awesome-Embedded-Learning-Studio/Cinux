# F-USABILITY 批3a-2:force_sig SMP 安全 + vfork exit 显式释放

> 2026-07-03,分支 `feat/f-usability`。codex `ec6d4b0` 跑通 gcc driver 单命令后的即时
> follow-up —— 消 review 发现的 SMP 隐患(本仓库默认多核、未来迸发更多 CPU,不留坑)。

## 背景

review codex `ec6d4b0` 发现两处规范/隐患:

1. **force_sig 改共享 disposition(SMP 隐患)**:`signal_force_send`(force=true)在
   `queue_signal` 里直接 `sig_set_del(target->sig_blocked)` + 把
   `target->sig_actions->actions[sig].type` 从 kIgnore 改 kDefault。`sig_actions` 是
   `SharedSigActions`(CLONE_SIGHAND 多 task 共享 + 引用计数),SMP 下无锁写共享
   disposition (a) 与读路径竞态撕裂 (b) 永久破坏全组 disposition。
2. **vfork_parent exit 不显式处理**:只在 `execve` 成功时 unblock,字段注释承诺
   "execs or exits" 但 exit 路径不显式释放(经 sys_exit→unblock(parent) 间接覆盖,
   字段 dangling)。

## 改动

### 1. force_sig 改 per-task sig_forced(对齐 Linux `force_sig_info`)

force 是**投递时行为**,不是状态修改:

- `process.hpp`:`Task` 加 `SigSet sig_forced{0}`(per-task 私有)。
- `signal.cpp queue_signal` force 分支:只 `sig_set_add(target->sig_forced, sig)`,
  **不改 sig_blocked / 不改 sig_actions**。
- `signal_pick_deliverable`:`avail = (pending & ~blocked) | forced`(forced 绕过 block
  mask)。
- 两条 deliver 路径(`signal_check_and_deliver` + `signal_check_deliver_isr`):开头查
  `sig_forced` 位 + 清位(consume);`forced + kIgnore → signal_exec_default`(绕过
  SIG_IGN,防同步 fault 死循环回原 RIP)。
- `fork.cpp` / `clone.cpp`:child 清 `sig_forced = 0`(per-task,不继承)。

**SMP 安全**:`sig_forced` 是 per-task,force 写自己的,绝不碰共享 `sig_actions`。
既有债(sig_pending / sig_blocked 全原子化 + 信号锁)排独立 follow-up。

### 2. vfork_parent exit 显式释放

`sys_exit.cpp`:child exit 时 `if (vfork_parent) { unblock(vfork_parent); vfork_parent =
nullptr; }`。`unblock` 幂等(与既有 `unblock(parent)` 重复无害,注释已证)。防 dangling
+ 对齐注释承诺 "execs or exits"。

## 验证

- `run-kernel-test-all` 两 leg ALL PASSED,**1108/0**(check_test_count 门)—— 含
  **-smp 2 leg,SMP 安全验证**。
- host 单测 **69/0**(改公共头 `process.hpp` 后全量)。
- `run-buildroot-usability` gcc gate PASS:`Hello from GCC!` + `[usability] PASS
  gcc-compile-run` + result PASS —— force_sig 改动**不破坏** codex gcc driver 修复。

## GOTCHA

1. **force 是投递时行为,不是状态修改**:Linux `force_sig_info` 不改共享 disposition;
   CinuxOS 对齐用 per-task `sig_forced` 位图,deliver 时 consume。**别在 queue 阶段改
   sig_actions**(共享写,竞态 + 误伤同组线程)。
2. **sig_forced 必须在 deliver 开头清(consume)**:否则下次同信号误判 forced。两条
   deliver 路径(`signal_check_and_deliver` syscall 返回 + `signal_check_deliver_isr`
   中断返回)都要清。
3. **既有信号 SMP 债**:`sig_pending` / `sig_blocked` 是普通 `uint64_t` 位图无原子,
   SMP 投递路径读它们有竞态。本批只修 force 引入的新债,既有债(信号子系统全原子化
   + siglock)排 follow-up。
4. **tmpfs 文件头**:codex 删了详细 doc block 但保留单行 `@file @brief`,符合
   CODING-TASTE L35(只要求「文件头 Doxygen @file @brief 说明模块职责」),不需补回。

## follow-up

- 既有信号 SMP 债(sig_pending/sig_blocked 原子化 + per-task 信号锁)。
- unhandled syscall stub 降噪(318 getcpu / 435 clone3 / 273 rseq 等 glibc probe)。
