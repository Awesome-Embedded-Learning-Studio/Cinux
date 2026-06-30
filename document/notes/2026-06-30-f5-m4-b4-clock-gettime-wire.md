# F5-M4 批4 — 接 sys_clock_gettime + 漂移修正(HPET/RTC 上线)

> 2026-06-30。F5-M4 第四批:把 HPET(monotonic)+ RTC(墙钟基线)接进 `sys_clock_gettime`,
> 替掉两边都读 PIT uptime、无墙钟的旧实现。

## 背景

批2/批3 的驱动到位,但 `do_clock_gettime_kernel` 还在读 `PIT::get_uptime_ms()`。本批接线:
MONOTONIC←HPET,REALTIME←RTC boot epoch + HPET delta(「漂移修正」),并删掉「CinuxOS 无墙钟」
的过时注释。

## 设计

[sys_clock_gettime.cpp](../../kernel/syscall/sys_clock_gettime.cpp):

```cpp
uint64_t mono = monotonic_ns();  // HPET,不可用退回 PIT uptime
uint64_t ns   = (REALTIME) ? rtc.boot_epoch_ns() + mono : mono;
ts.tv_sec = ns/1e9; ts.tv_nsec = ns%1e9;
```

### 漂移修正 = RTC 粗秒 + HPET 连续推进

REALTIME 不是每次重读 RTC(端口 I/O 慢 + 周期重同步需 IRQ,留 follow-up)。而是 **boot 时读一次
RTC 锁定绝对秒基**,之后靠 HPET monotonic delta 给亚秒精度 + 持续推进。代价:长时间运行 HPET
相对 RTC 会漂(无 NTP 式周期重同步)——诚实标注,不包装成 NTP 级。

### 退化策略(可用优先)

- HPET 不可用 → MONOTONIC 退回 `PIT::get_uptime_ms()*1e6`(无 HPET 硬件零回归,PIT 本就在跑);
- RTC 未读 → REALTIME 退回纯 monotonic。
- 两者都不在 → 返回 0,不崩。

`main.cpp` Step11b(AddressSpace 后、framebuffer 前,VMM+ACPI 都已起)插 `g_hpet.init()` +
`g_rtc.init()`。

### 测(test_syscall.cpp 加 2 例)

- `test_sys_clock_gettime_realtime_uses_rtc`:REALTIME `tv_sec` 落 [2024-01-01, 2100-01-01)
  ——证 RTC 真喂进去了(不是 boot-relative 小值);
- `test_sys_clock_gettime_realtime_ahead_of_monotonic`:REALTIME > MONOTONIC(漂移修正契约)。
- 老 `test_sys_clock_gettime_fills_timespec`(MONOTONIC tv_sec≥0)不回归。

## 验证

两 leg run-kernel-test-all 999/0(997 + 2 新 clock_gettime 测)。production boot smoke
(`make run` 等价)零 panic:`[HPET]`/`[RTC]` 正常起,kernel_init 正常退出。两 kernel(test +
production main.cpp)都编过。

## 收获

接缝很干净:两驱动自持状态,sys_clock_gettime 只组合——没建 `kernel/lib/time.hpp` 大杂烩
(YAGNI,用户批表也没列)。PIT 不动(仍 BSP 抢占),只在 HPET 缺席时当 monotonic 兜底。
F5-M4 至此 HPET+RTC 全链路打通,follow-up(周期中断/nanosleep/RTC 重同步)见 [[2026-06-30-f5-m4-b2-hpet-mmio-driver]]。
