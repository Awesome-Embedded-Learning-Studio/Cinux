# PIT mode 3→2 修 IRQ0 2x 触发(全系统走时修正)

> 日期：2026-07-12 · commit `490d8ac` · v1.0.0 发版后真实可用化 · 状态：✅ 合 main

## 现象

实现真 `setitimer(ITIMER_REAL)` 驱动 busybox ping 每秒发包后，HPET 真实时钟测得 SIGALRM fire 间隔
**505ms（应 1s）**——全系统走时 **2x 快**。uptime / sleep / scheduler quantum 全 2x 错。

## 根因

Cinux PIT 原用 **mode 3**（`0x36` square wave）。mode 3 counter decrement-by-2，每周期到 0 **两次**
（半周期一次），QEMU 在每次 terminal count 都抬 IRQ0 → 实际 200Hz（配的 100Hz）。

**为何潜伏这么久**：没 1s 参照。setitimer 一直是 stub（ping 只发 1 包），走时 2x 无人察觉。直到真
setitimer 的 1s 间隔被 HPET 量出来才暴露。

## 修复

`CMD_MODE_3`（0x06）→ **`CMD_MODE_2`**（0x04，rate generator）。mode 2 decrement-by-1，每周期 1 次
terminal count → 恰 `freq_hz_` IRQ/s。Linux 时钟源就是这个模式。

IOAPIC IRQ0 是 edge-triggered 上升沿（`set_redirect` 默认 level_triggered=false active_low=false），
mode 2 每周期 1 负脉冲 1 正脉冲，上升沿 1 次/周期 → 1x。

## 验证

HPET `g_hpet.monotonic_ns()` 测得 setitimer SIGALRM fire 间隔归正为 ~1s；两 leg run-kernel-test-all 绿。

## 教训

- 实现定时器类功能，用 HPET `monotonic_ns()`（真实时钟）验证 fire 间隔，**别信 PIT tick_count**（它自己也 2x）。
- mode 3 square wave 的「每周期 2 边沿 / 2 terminal count」是经典坑；周期性时钟源该用 mode 2。
- 潜伏期 = 没有独立参照；一旦有了真 1s 参照（setitimer）才显形。

## 关联

- [2026-07-12-net-ping-ctrl-c-usability](2026-07-12-net-ping-ctrl-c-usability.md) — setitimer 是 ping 弧的组成，2x 坑在那时暴露。
