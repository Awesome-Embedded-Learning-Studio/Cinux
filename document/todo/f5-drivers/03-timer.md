# M4: HPET + RTC 定时器

> HPET 高精度定时器 + RTC 实时时钟。
> HPET 作为高精度时间源补充 APIC Timer，RTC 提供日期时间。

> **本轮范围栅栏（F5-M4，2026-06-30 立项）**：只做 **free-running 计数器纯读 + 端口 RTC**，目标是给 `sys_clock_gettime` 接真时间源（MONOTONIC←HPET、REALTIME←RTC）。**不做**：HPET 周期中断（T1 的 timer channel / 中断集成，需 comparator + IRQ vector，类比 LAPIC timer，留 follow-up）、nanosleep / gettimeofday（T4 留后续）、`kernel/lib/time.hpp` 大杂烩（T2，YAGNI——两驱动自持状态由 sys_clock_gettime 组合）、RTC 周期重同步（NTP 式，需 IRQ）。PIT **不动**（仍独占 BSP 抢占）。下面清单中本轮勾的是 T1 的 MMIO 映射/counter/频率/纳秒 API + T3 的 CMOS/BCD/24h/世纪/UIP/epoch + T4 的 clock_gettime 两时钟；timer channel 配置、中断集成、nanosleep 留空。

## 任务清单

### T1: HPET 驱动

**文件**: `kernel/drivers/hpet/hpet.hpp`, `kernel/drivers/hpet/hpet.cpp`

从 ACPI HPET 表获取 MMIO 地址：

```cpp
namespace cinux::drivers {

class HPET {
public:
    void init(uint64_t mmio_base);

    // 计数器
    uint64_t counter();              // 当前计数值
    uint64_t frequency();            // Hz
    uint64_t nanoseconds_per_tick();

    // 定时器通道（0-2 可用，3+ 可选）
    void timer_set(uint32_t channel, uint64_t ns, bool periodic, uint32_t irq_vector);
    void timer_stop(uint32_t channel);
    void timer_enable(uint32_t channel);
    void timer_disable(uint32_t channel);

    // 时间转换
    uint64_t ticks_to_ns(uint64_t ticks);
    uint64_t ns_to_ticks(uint64_t ns);

private:
    volatile struct HPETRegs* regs_;
    uint64_t period_;  // femtoseconds per tick
    uint64_t freq_hz_;
};

extern HPET g_hpet;

} // namespace cinux::drivers
```

**HPET 寄存器布局**:
| 偏移 | 名称 | 用途 |
|------|------|------|
| 0x000 | General Capabilities | period, vendor, count size |
| 0x010 | General Configuration | ENABLE, LEGACY_RT_ROUTE |
| 0x020 | General Interrupt Status | 中断状态 |
| 0x0F0 | Main Counter Value | 64-bit 自由运行计数器 |
| 0x100+0x20*n | Timer n Config | 定时器通道配置 |
| 0x108+0x20*n | Timer n Comparator | 比较值 |

- [x] HPET MMIO 映射（从 ACPI HPET 表获取地址）  <!-- F5-M4 批2 -->
- [x] counter() 读取主计数器  <!-- F5-M4 批2 -->
- [x] 频率计算（从 period 字段，单位 femtoseconds）  <!-- F5-M4 批2 -->
- [ ] 定时器通道配置（one-shot + periodic）  <!-- follow-up: 周期中断 -->
- [ ] 中断集成（配置路由到 APIC）  <!-- follow-up: 周期中断 -->
- [x] 纳秒级时间 API  <!-- F5-M4 批2: hpet::monotonic_ns -->

### T2: 系统时间 API

**文件**: `kernel/lib/time.hpp`

```cpp
namespace cinux::lib {

// 获取系统时间（纳秒精度）
uint64_t time_ns();

// 获取系统时间（微秒精度）
uint64_t time_us();

// 获取系统时间（毫秒精度）
uint64_t time_ms();

// 获取 Unix 时间戳（秒）
uint64_t time_epoch();

} // namespace cinux::lib
```

- [ ] 基于 HPET counter 实现 time_ns/us/ms
- [ ] 启动时从 RTC 读取 epoch 偏移
- [ ] 与现有 PIT get_uptime_ms() 对齐

### T3: RTC 驱动

**文件**: `kernel/drivers/rtc/rtc.hpp`, `kernel/drivers/rtc/rtc.cpp`

CMOS RTC 通过端口 0x70/0x71 访问：

```cpp
struct DateTime {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
};

class RTC {
public:
    void init();
    DateTime read();
    uint64_t epoch_seconds();  // 转为 Unix 时间戳
};

extern RTC g_rtc;
```

**CMOS 寄存器**:
| 偏移 | 内容 |
|------|------|
| 0x00 | 秒（BCD） |
| 0x02 | 分钟（BCD） |
| 0x04 | 小时（BCD） |
| 0x06 | 星期 |
| 0x07 | 日（BCD） |
| 0x08 | 月（BCD） |
| 0x09 | 年（BCD） |
| 0x0B | 状态 B（24h 模式、BCD 格式） |

- [x] CMOS 端口读取  <!-- F5-M4 批3 -->
- [x] BCD → 二进制转换  <!-- F5-M4 批3 -->
- [x] 24h/12h 模式检测  <!-- F5-M4 批3 -->
- [x] 世纪处理（2000+）  <!-- F5-M4 批3 -->
- [x] epoch_seconds() 转换  <!-- F5-M4 批3 -->
- [x] RTC 更新完成检测（UIP bit）  <!-- F5-M4 批3 -->

### T4: Syscall 集成

- [x] clock_gettime(CLOCK_REALTIME) — 基于 HPET + RTC epoch  <!-- F5-M4 批4 -->
- [x] clock_gettime(CLOCK_MONOTONIC) — 基于 HPET counter  <!-- F5-M4 批4 -->
- [ ] gettimeofday() — clock_gettime 封装  <!-- follow-up -->
- [ ] nanosleep() — 基于 HPET 定时器通道  <!-- follow-up: 需周期中断 -->

### T5: 单元测试

- [ ] HPET counter 单调递增
- [ ] HPET 定时器周期中断
- [ ] RTC 读取合理日期
- [ ] time_ns() 精度验证
- [ ] nanosleep 睡眠精度

## 产出物

- [ ] `kernel/drivers/hpet/hpet.hpp` / `.cpp`
- [ ] `kernel/drivers/rtc/rtc.hpp` / `.cpp`
- [ ] `kernel/lib/time.hpp` — 系统时间 API
- [ ] clock_gettime / gettimeofday / nanosleep syscall
- [ ] CMakeLists.txt 更新
