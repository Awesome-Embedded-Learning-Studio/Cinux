# F5-M4 批3 — CMOS RTC 驱动(0x70/0x71 BCD → Unix epoch)

> 2026-06-30。F5-M4 第三批:MC146818 兼容 RTC 端口 I/O,BCD→binary,转 Unix epoch,
> boot 时缓存为墙钟基线(给批4 REALTIME 用)。

## 背景

REALTIME 要真墙钟。CMOS RTC 经典 0x70/0x71 端口,默认 BCD。读窗口要避开 1Hz 寄存器更新
(UIP)。`io_inb/io_outb`([io.hpp](../../kernel/arch/x86_64/io.hpp))现成。

## 设计

[rtc.hpp](../../kernel/drivers/rtc/rtc.hpp) + [rtc.cpp](../../kernel/drivers/rtc/rtc.cpp):

- 纯算术 **inline**(host 可测):`bcd_to_binary`(0x26→26)、`days_from_civil`(Howard Hinnant
  proleptic Gregorian,精确无溢出)、`datetime_to_unix_seconds`。
- `read_reg(index)`:`outb(0x70, 0x80|index)`(bit7 关 NMI 防 read 中途 NMI 踩 RTC)→ `inb(0x71)`。
- `read_datetime()`:读 Status B 判 BCD/binary + 12/24h 模式;**UIP(Status A bit7)轮询 +
  两读一致**(防撕裂,4 轮封顶——测试内核全程 cli,QEMU 更新 1Hz 慢,立刻收敛)。
  - 12h 模式 PM 标志在 raw hours 字节 bit7(BCD 解码前 mask 掉,解码后加 12)。
  - 世纪 reg 0x32;无世纪寄存器则回退 2000s。
- `init()`:读一次 → 缓存 `boot_epoch_seconds_`,available = epoch>0。

### 机制测(test_rtc.cpp)+ host 算术测(test_rtc_math.cpp)

- 实机:读出 sane 日期(year 2024-2099 / month / day / h<24 / 分秒<60)+ boot epoch
  ∈[2024-01-01, 2100-01-01)。
- host:bcd 解码 + epoch 已知日对照(1970=0 / 2000-01-01=946684800 / 2024-01-01=1704067200
  / 闰日 / 时分秒累加)。

## 验证

两 leg run-kernel-test-all 997/0(995 + 2 RTC 测)。host rtc_math 6/6。
**QEMU 实测 `[RTC] 2026-06-30 06:35:58 (epoch 1782801358)`**——读出真实当前日期(BCD + 世纪
+ epoch 全对,与宿主时钟一致)。

## 收获

端口 I/O 直白,关键在「防撕裂」(UIP + 两读一致)和「12h/BCD 模式自适应」(别假设 24h BCD,
读 Status B 判)。`days_from_civil` 不手搓——用 Hinnant 算法,精确且 host 可验证。RTC boot
只读一次(端口 I/O 慢 + 周期重同步需 IRQ,留 follow-up);REALTIME 靠批4 加 HPET delta 推进。
