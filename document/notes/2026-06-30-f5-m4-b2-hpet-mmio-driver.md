# F5-M4 批2 — HPET MMIO 驱动(free-running 计数器 → monotonic ns)

> 2026-06-30。F5-M4 第二批:映射 HPET 寄存器块,启用主计数器,读出 boot-relative
> monotonic 纳秒。两个 GOTCHA 都在这批被实机教训出来——接手 HPET 必读本篇。

## 背景

批1 解出 HPET 在物理 0xFED00000。本批把它映进 KMEM_MMIO 窗(KMEM_MMIO_BASE+0x60000,
FLAG_PCD uncached,照 LAPIC/xHCI/e1000 范式),读周期、置 ENABLE、捕获 boot 计数器基线,
暴露 `monotonic_ns()`。

## 设计

- 驱动 [hpet.hpp](../../kernel/drivers/hpet/hpet.hpp) + [hpet.cpp](../../kernel/drivers/hpet/hpet.cpp):
  `init()` 幂等(find_table→parse_hpet→g_vmm.map→读周期→置 ENABLE_CNF→捕获 boot_counter);
  `monotonic_ns()` = `ticks_to_ns(period_fs, now - boot_counter_)`。
- `ticks_to_ns(period_fs, ticks)` **inline 纯函数**(放头里,host 可测):ns = ticks×period/1e6,
  溢出安全拆成 `hi=ticks/1e6` / `lo=ticks%1e6` → `hi*period + lo*period/1e6`,中间积在
  uint64 内稳几百年(朴素乘法 100MHz 跑一年 3e15×1e7=3e22 溢出)。
- 机制测:test_period_is_sane(0<period≤1e8fs)、**test_counter_advances(读两次递增——证
  ENABLE 真生效,不是绿盖住冻死的设备)**、test_monotonic_ns_nondecreasing。host 5 例
  ticks_to_ns(100MHz/1ns tick/年不溢出)。

## GOTCHA(两个,都是实机教训出来的)

### GOTCHA-1:General Config 寄存器在 0x010,不是 0x008

HPET 通用寄存器**间距 0x10**:[63:0]=vendor/rev、[63:32]=period(femtoseconds)。

```
0x000  General Capabilities (8B)  —— 低32=vendor/rev,高32=period
0x010  General Configuration      —— bit0 = ENABLE_CNF
0x020  General Interrupt Status
0x0F0  Main Counter (64-bit)
0x100  Timer0 Config ...
```

我一开始按「密集布局」把 Config 放 0x008,**写 ENABLE 死活不生效**(cfg 读回恒 0)。诊断:
对 0x008 的写全丢(落在 reserved 区),对 0x000 的读却对(0x9896808086A201——高32=0x00989680
正是 100MHz 周期)。改成 0x010 立刻通。**QEMU hw/timer/hpet.c 的 `HPET_CFG=0x010` 与本布局一致。**

> 周期(period)在 Capabilities 的**高 32 位([63:32])**,不是低 32。低 32 是 vendor(0x8086=Intel)
> + 计时器数。我第一版 `& 0xFFFFFFFF` 取低 32 得到 0x8086A201 当成周期,被判「bogus」。
> 正解:`read32(0x004)`(GAS 高 32)直接拿 period。

### GOTCHA-2:全用 32-bit 访问,别用 64-bit 写

QEMU(和部分真机)**丢 64-bit MMIO 写**,虽然 64-bit 读碰巧能用(QEMU 内部拆成两次 32-bit
读再拼)。诊断证据:64-bit 写 Config(0x008 那次)读回 0;但 32-bit 写 Main Counter(0x0F0)
能写进 0xDEADBEEF 并读回。所以**一律 32-bit 访问**(Linux 范式):

- period: `read32(0x004)`(Caps 高半);
- ENABLE: `write32(0x010, read32(0x010) | 1)`;
- 64-bit Main Counter: 两半读 `read32(0x0F0)`/`read32(0x0F4)` + rollover 保护
  (高半变了就重读低半,标准无 race 算法)。

## 验证

两 leg run-kernel-test-all 995/0(991 + 4 驱动测)。host ticks_to_ns 5/5。
`[HPET] MMIO 0xFED00000, period 10000000 fs (100000000 Hz), counter enabled` 两 leg + production 都打。

## 收获

诊断套路:读对/写错 → 怀疑访问宽度(64-bit 写被丢);period 取错半 → 看 raw 64-bit 值定位
字段位置;寄存器写不生效 → 核对偏移(QEMU 源码 `HPET_CFG` 宏是权威)。每个机制测(读两次
递增)都是防「绿盖住冻死设备」的哨兵——见 [[2026-06-30-f5-m4-b1-hpet-acpi-table]] 的机制测纪律。
