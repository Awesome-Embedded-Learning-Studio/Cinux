# 2026-06-16 F1-M3 DMA — PrdtBuilder（批3）

## 背景

scatter-gather DMA 需把 buffer 拆成段列表喂设备。设备格式各异（AHCI `HBAPrdtEntry` 16 B / dbc 22-bit、NVMe PRP、VirtIO descriptor），但都需"按段大小上限拆分"。批3 提供设备无关的 segment 构建器，驱动各自转硬件格式。

## 设计

- `DmaSegment { uint64_t phys; uint64_t size; }`：设备无关段。
- `PrdtBuilder<MaxSegments>`：固定容量（template，匹配硬件表如 AHCI=8）。`add(phys, size, max_segment)` 按 max_segment 拆分累积；`add_buffer(DmaBuffer)` 适配；`fits()` 预检容量；满则丢弃（返回 0，不越界）。
- header-only template，无 PMM/VMM，纯逻辑。

## 关键决策

- **设备无关 > 绑 AHCI**：F1 基建定位，多设备复用（AHCI/NVMe/VirtIO）。F5-M1 AHCI 转 `HBAPrdtEntry`。
- **template MaxSegments**：编译期容量，匹配硬件表大小，零分配。
- **max_segment==0 防护**：`add` 返回 0，避免 `chunk = min(remaining, 0) = 0` 死循环。

## 验证

run-kernel-test **687 → 694**（+7）：单段、超限拆分（10 KiB @ 4 KiB → 3 段）、多次累积、满表丢弃、add_buffer、fits 预检、max==0 防护。

## 遗留

- 批4 收尾（memory_layout 注释语义化 + M3 总结 + 全量验证）
- AHCI 消费适配（F5-M1）：PrdtBuilder segment → `HBAPrdtEntry`

---

commit：`6426417`（批3 PrdtBuilder）。
