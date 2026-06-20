# F4-M4 M4-2-1（reschedule IPI 基础设施）— 2026-06-20

> M4-2 前半：把 reschedule IPI 的「通路」搭好（vector + stub + handler + IDT 注册 + 唤醒点）。
> **单核下完全 no-op**（无在线 AP → 不发 IPI），验证门 = run-kernel-test **875/0 不变**。
> 真正让 IPI 生效（AP 从 `cli;hlt` 改 `sti;hlt` + 真机跑任务）在 **M4-2-2**（高风险）。
> 分支 `feat/f4-m3-trampoline`。代码 commit `e8f0136`。

## 目标

让 AP 能被 IPI 唤醒 —— `add_task`/`unblock` 后，BSP 给 idle AP 发一个 reschedule IPI，把它从 `hlt` 踢醒去共享 runq 取任务。M4-2-1 只搭通路，不碰 AP idle loop（AP 仍 `cli;hlt`，IF=0，即使收到 IPI 也不响应；待 M4-2-2 改 `sti;hlt` 后通路才真正生效）。

runq 无需新建：`default_rr_` 是全局单例，所有 CPU 共用 + `RoundRobin::lock_` 跨核互斥已就绪（Phase 1/2 遗产）。

## 改动（5 文件，+73 行）

| 文件 | 改动 |
|------|------|
| [smp.hpp](../../kernel/arch/x86_64/smp.hpp) | `constexpr uint8_t kRescheduleIpiVector = 0xE0` + `void wake_idle_ap()` 声明（namespace cinux::arch） |
| [interrupts.S](../../kernel/arch/x86_64/interrupts.S) | irq15_stub 后加 `ISR_NOERRCODE reschedule_ipi_stub, reschedule_ipi_handler`（照 IRQ stub 模式，带 signal_check_deliver_isr） |
| [ap_main.cpp](../../kernel/arch/x86_64/ap_main.cpp) | `reschedule_ipi_handler`（纯 `g_lapic.eoi()`）+ `wake_idle_ap`（遍历在线 AP 发 IPI） |
| [irq_handlers.cpp](../../kernel/arch/x86_64/irq_handlers.cpp) | extern `reschedule_ipi_stub` + include smp.hpp + `irq_init` 末尾注册 vector 0xE0 |
| [scheduler.cpp](../../kernel/proc/scheduler.cpp) | include smp.hpp + `add_task`/`unblock` 末尾调 `arch::wake_idle_ap()` |

## 设计决策

1. **vector 0xE0**：避开 PIC IRQ 段（0x20-0x2F）、spurious（0xFF）、sigreturn trap（0x80）。0xE0 在空白区。
2. **handler 纯 EOI no-op**：真正的 reschedule 在 AP idle loop（M4-2-2）做 —— IPI 只负责把 AP 从 `hlt` 踢醒，stub 返回后 idle loop 重跑 `schedule()`。IPI 是 LAPIC vector，EOI 直走 `g_lapic.eoi()`，**不走** `irq_eoi()`（后者按 IRQ 线号 dispatch）。
3. **多发 IPI，不做精确 idle 跟踪**：`wake_idle_ap` 无脑给所有在线 AP 发 IPI。冗余 IPI（发给正在忙的 AP）无害 —— idle loop 重检 runq，空则再 hlt。代价是少量 spurious wakeup，换来**免精确 per-CPU idle 标志**（其 BSP 读 / AP 写竞态需额外打理，交接笔记 M4-2-1.6 已倾向简化）。
4. **不加 `bool idle` 字段**：交接笔记 M4-2-1.5 原列加 PerCpu `bool idle`，但因选「多发 IPI」策略，M4-2-1 没有任何代码读写它 → 加了是 dead field。按 YAGNI 推迟到 M4-2-2（若决定精确跟踪再加）。M4-2-1 聚焦「IPI 通路 + 单核 no-op」。
5. **唤醒点在 kprintf 后、锁外**：`add_task`/`unblock` 调 `wake_idle_ap` 时，runq 锁（`enqueue` 内部 `irq_guard`）已释放，`send_ipi`（poll delivery status）不持锁、不阻塞。

## 单核为何 no-op

`wake_idle_ap` 读 `g_aps_online`（SEQ_CST），单核 `boot_aps` 因 `cpu_count<=1` 提前返回 → `g_aps_online==0` → 循环 `cpu=1; cpu<=0` 不执行 → 不发任何 IPI。vector/stub/handler 注册到 IDT 但从不触发。故 875/0 不变。

## 跨核可见性

`percpu_blocks[cpu].apic_id` 由该 AP 在 `ap_main` 里设（`pcpu->apic_id = g_lapic.id()`），**在** `__atomic_add_fetch(&g_aps_online, SEQ_CST)` **之前**。BSP `wake_idle_ap` 用 `__atomic_load_n(&g_aps_online, SEQ_CST)` 看到 `online>=cpu` 时，该 AP 早先的 `apic_id` 写入对其可见（release/acquire）。`id()` 返纯 ID（`>>24`），`send_ipi` 内部 `<<24` 写 ICR high，格式匹配（[local_apic.cpp:44/72](../../kernel/drivers/apic/local_apic.cpp#L44)）。

## 验证

- 全量 `cmake --build build -j$(nproc)` 通过（改公共头 smp.hpp，scheduler/ap_main/irq_handlers 都 include）。
- run-kernel-test：**875 passed, 0 failed**（与 M4-3 基线完全一致）。`[SCHED] ... unblocked` 正常出现（unblock 唤醒点调了 wake_idle_ap，单核 no-op 无 IPI 日志）。无 panic/GP（仅预期 #BP 断点测试 + #PF-skip）。

### 环境坑：本机 KVM 无权限 → CINUX_NO_KVM 仅 configure 时生效

本次 session 起本机 `/dev/kvm` group=20 但当前用户**不在 kvm 组** → `-accel kvm` 报 `Permission denied`，QEMU 直接退出（run-kernel-test target 不报错，因它只管启动 QEMU）。

`CINUX_NO_KVM` 是 **cmake configure 时**判断（[qemu.cmake:10](../../cmake/qemu.cmake#L10) `if(EXISTS "/dev/kvm" AND NOT DEFINED ENV{CINUX_NO_KVM})`），运行时 `CINUX_NO_KVM=1 cmake --build` **不重新 configure**，`QEMU_ACCEL` 已 cache 成 kvm → 无效。

**解法（不污染 cache）**：手动调 wrapper，把 `-accel kvm -cpu max` 换 `-accel tcg`：
```bash
cmake --build build --target test-image regenerate-ext2-image -j$(nproc)  # 确保镜像含最新改动
timeout 400 bash -c 'scripts/qemu_test_wrapper.sh qemu-system-x86_64 \
  -m 8G -serial stdio -no-reboot -debugcon file:build/debug.log -global isa-debugcon.iobase=0xe9 \
  -accel tcg -vnc :0 -usb -device usb-tablet \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
  -device ahci,id=ahci -drive file=build/ahci_test.img,format=raw,if=none,id=ahci-disk -device ide-hd,drive=ahci-disk,bus=ahci.0 \
  -drive file=build/ext2.img,format=raw,if=none,id=ext2-disk -device ide-hd,drive=ext2-disk,bus=ahci.1 \
  -drive file=build/cinux_test.img,format=raw,index=0,media=disk'
```
TCG 全软件模拟，875 测试 ~3-4 分钟（KVM 下 60-90s），timeout 给足。wrapper 把 isa-debug-exit 码映射（exit 1→success / 3→fail）。**修法二**：`CINUX_NO_KVM=1 cmake -B build -S .` 重新 configure 切 TCG（会改 cache，用户修好 kvm 权限后 unset 重 configure 即可回 KVM）。

> 用户修好 kvm 组权限（`sudo usermod -aG kvm $USER` 后重登）即可直接用 `run-kernel-test` target 走 KVM。

## 相关 GOTCHA

- **#23（既有）**：context_switch 恢复点 current 读 per-CPU。M4-2-1 不碰，但 M4-2-2 AP idle loop 首次切换直接适用。
- **#24（既有，M4-3）**：prepare-to-wait `schedule_blocked` 须在 irq_guard 析构后。M4-2-2 AP 真跑并发是它的实战验证对象。

## 不做（M4-2-2 / follow-up）

- AP idle loop `cli;hlt`→`sti;hlt` + 首次切换 prev（AP idle task）→ **M4-2-2**。
- `-smp 2` 真机 AP1 真跑任务 / 多线程不挂 → **M4-2-2 收官门**。
- per-CPU `bool idle` 精确跟踪（若 M4-2-2 决定需要）。
- per-CPU APIC timer 时间片抢占（用户已定不做，follow-up）。

## 参考

- 交接：[2026-06-20-f4-m4-2-handoff.md](2026-06-20-f4-m4-2-handoff.md)（M4-2 拆批 + 精确文件/行）。
- IPI 基础（send_ipi/init/sipi）：Phase 2 [2026-06-19-f4-m3-p2-ap-boot.md](2026-06-19-f4-m3-p2-ap-boot.md)。
- Linux `smp_send_reschedule` / `reschedule_interrupt` 范式。
