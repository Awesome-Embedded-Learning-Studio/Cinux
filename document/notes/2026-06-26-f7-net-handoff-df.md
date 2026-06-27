# F7 shell ping 交接 — ring3 #DF 根因 + 修法方向

**日期**: 2026-06-26　**分支**: `worktree-f7-net-ping`（10 commit，未 push）
**给接手 AI**：F7 网络栈本体（L0-L3）已 100% 交付且全绿；**shell ping（B1-B3）代码齐 + 内核态证了，但 ring3 真跑会 #DF**。本文档说清 #DF 根因 + 怎么修，别动协议栈。

## 现状（诚实）
| 模块 | 状态 | 说明 |
|------|------|------|
| L0-L3（netdev 抽象 + ARP/IPv4/ICMP + loopback + e1000 + 解耦 grep） | ✅ 完整 | run-kernel-test 949/0、host net 5/5、真 ping 10.0.2.2 reply id=0x1234。**协议栈扎实，勿动。** |
| B1 生产 `cinux::net::init()`+`ping()`（`c0c8ddd`） | ✅ 代码+内核态证 | `test_production_ping` reply id=0xbeef |
| B2 `SYS_ping` 系统调用（`b0c817f`） | ✅ 代码+内核态证 | `test_syscall_ping` rc=0（**内核态直调，不走 ring3**） |
| B3 shell `cmd_ping`（`98990a4`） | ✅ 编链通过 | `make run` boot 起栈、shell 起、无 panic（只要不敲 ping） |
| **ring3 shell 敲 ping** | ⛔ **#DF** | 见下 |

## #DF 根因（boot smoke 实测）
`make run`（非 GUI 构建，shell 自动起）里 `/bin/sh` 跑 `cmd_ping → int$0x80 → sys_ping → cinux::net::ping()`：
```
jumping to user mode: entry=0x400270 ... Shell spawned pid=1
========== KERNEL PANIC: Double Fault ==========
```
- `cinux::net::ping()`（`kernel/drivers/net/net_init.cpp`）的等待循环是 **`sti; hlt; cli`**（复自 test_ping_e1000）。
- 在 **ring3 syscall 上下文**（ring0、内核栈上）里 `sti` → timer IRQ 中途打进來 → **调度器在 syscall 中途抢占换出该任务** → 破坏 syscall 栈帧 → `sysretq` 时 #GP→#DF。
- **不是协议 bug**：#DF 发生在 sti/hlt 等待处，在 ARP/ICMP 帧收发之前。`ping()` 函数本身对（`test_production_ping` 内核态调它拿 reply）。
- **harness 抓不到**（用户的洞察）：`run-kernel-test` 不能真跑 ring3 子任务——`test_syscall_ping` 是内核态直调 handler，绕过 syscall 入口 + sti/hlt，所以"绿"了但 ring3 实跑崩。**只有 QEMU boot smoke（复用 `/bin/sh` 的 fork→execve→ring3 路径当探测器）能抓到。**

## 修法方向（不要继续 sti/hlt 自旋）
sti/hlt 在 ring3 syscall 里跟抢占式调度器不兼容。**正确做法 = 阻塞式 ping + 常驻 poll driver**（我之前 defer 的模型，现在证明是必须的）：
1. **常驻 poll driver**：内核线程（`TaskBuilder` 起一个 net 线程）或挂 LAPIC/PIT tick，循环 `NetStack::poll()`。production IF=1，tick 自然驱动 QEMU main loop → SLIRP 投递 → poll 派发。**不需要 sti/hlt**。
2. **`sys_ping` 改阻塞**：发 echo 后把当前任务 block（复用 F3 的 futex/wait 机制，`prepare_to_wait`+`schedule_blocked`），不是自旋。poll driver 收到 echo-reply → `IcmpModule` 记录 → 唤醒等待的 ping 任务 → `sys_ping` 返回。
3. `cinux::net::ping()` 拆成「发 echo + 注册 waiter」+「被 poll driver 唤醒后取结果」；删掉 sti/hlt 循环。
- 参考：F4-M5 的 `prepare_to_wait`/`schedule_blocked`（Mutex/Sem/futex/waitpid 四处已用）；F3-M2 futex 的 block/wakeup。
- loopback 内核测（`test_ping_loopback`）的同步 poll 仍可用（loopback 一次 poll 跑完，不需阻塞）——别回归它。

## 不要碰的（已扎实交付）
- `kernel/net/`（net_types/buffer/net_device/protocol_handler/net_stack/arp/ipv4/icmp/loopback_device）：协议层，host 单测 + 内核测全证。
- `kernel/drivers/net/e1000_net_device.hpp`（adapter）、`e1000_init.cpp`（+start_tx）：e1000 集成。
- `check_net_decoupling`（4 grep）：解耦机器执行。
- 复用 Cinux-Base：`Span`(FrameView)/`ScopeGuard`/`internet_checksum`。

## 复现 #DF 的 smoke
非 GUI 构建 + shell 自动 ping（已撤 auto-ping，源码干净；要复现就临时在 `_start` 加 `cmd_ping` 或交互敲）：
```
cmake -B build_smoke -DCMAKE_BUILD_TYPE=Release -DCINUX_GUI=OFF -DCINUX_USB=OFF
cmake --build build_smoke -j$(nproc)
timeout 20 cmake --build build_smoke --target run   # grep 'Double Fault'
```

## 入口文档
- 各层 note：`document/notes/2026-06-26-f7-net-{l0,l1,l2,shell-ping}-*.md`
- PLAN「✅ F7」+「✅ shell ping」段；ROADMAP F7-M1/M2/M3✅
- memory `f7-net-foundation-first`（架构要点 + GOTCHA）
