# F5-M4 批1 — ACPI HPET 表解析

> 2026-06-30。F5-M4 HPET+RTC 第一批:把 HPET 的 ACPI 描述表("HPET")纳入
> 现有 ACPI 表层,解出 MMIO 物理基址(0xFED00000),给批2 的 MMIO 驱动铺路。

## 背景

`sys_clock_gettime` 现状([sys_clock_gettime.cpp:37](../../kernel/syscall/sys_clock_gettime.cpp)):MONOTONIC/REALTIME 都读 `PIT::get_uptime_ms()`,无墙钟。F5-M4 给内核接高精度
monotonic(HPET)+ 真墙钟(RTC)。PIT 不动(仍独占 BSP 抢占)。

HPET 的寄存器块物理基址不在硬编码里,由固件 ACPI "HPET" 表描述。`acpi::find_table`
([acpi.hpp:92](../../kernel/drivers/acpi/acpi.hpp))注释早列了 HPET,是天然发现缝——
照抄 `parse_madt` 的套路加一个 `parse_hpet`。

## 设计

### HPET 表布局(56 字节)

[acpi.hpp](../../kernel/drivers/acpi/acpi.hpp) 加 `HPETHeader`(`[[gnu::packed]]`):

```
偏移  字段
0     SDTHeader (36B,"HPET")
36    block_id (4B:vendor/rev/计时器数/计数器位宽)
40    Base Address GAS(12B:space/bitwidth/bitoffset/accesssize/address)
52    hpet_number(1B) / min_tick(2B) / page_protection(1B)
```

**关键**:Base Address 是个 **ACPI Generic Address Structure(GAS,12 字节,不是 8)**——
`space(1)+bitwidth(1)+bitoffset(1)+accesssize(1)+address(8)`。物理基址在 +44 那 8 字节。
没单建 `GenericAddress` 类型(YAGNI,只有 HPET 用);直接把 GAS 字段拍进 `HPETHeader`。

`HPETInfo { uint64_t address; bool present; }`,`parse_hpet` 校验 `length>=56` + `base_addr_space==0`(系统内存)后返回 address。null/过短 → present=false。

### 机制测(test_hpet.cpp,跑在 QEMU)

- `find_table("HPET")` 命中(QEMU 'pc' 默认带 HPET 表);
- `parse_hpet` 解出 `address == 0xFED00000`;
- null / 过短(length=36)→ absent。

## 验证

两 leg run-kernel-test-all 991/0(基线 986 + 5 HPET 表测)。`parse_hpet` 在真 QEMU 解出
0xFED00000。docs-only 立项批(B0)+ 本批各一 commit。

## 收获

照抄 `parse_madt` 范式(校验 + reinterpret + 解字段)很顺;HPET 表只有「GAS 是 12B 不是 8B」
一个布局坑。真正难啃的 MMIO 访问留到批2(见 [[2026-06-30-f5-m4-b2-hpet-mmio-driver]])。
