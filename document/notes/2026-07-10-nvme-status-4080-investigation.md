# NVMe status=0x4080 协议取证

> 日期：2026-07-10 · FC29000 上游 NVMe I/O 偶发失败 · 状态：cache 假说否定，CID + QEMU LBA trace 待 GUI 复现

## 已否定的前提

故障日志：

```text
[NVMe] NVM I/O opcode=0x2 nsid=1 slba=920 nlb=2 failed status=0x4080
```

`io_submit()` 返回 `cqe.status >> 1`，只移除 phase bit，保留 SC、SCT、CRD、M、DNR。故 `0x4080` 是合法的 `DNR=1, SCT=0, SC=0x80 (LBA Out of Range)`，不是「正常 status 必须小于 0x100」。

当前 QEMU 源码进一步确认：

1. `nvme_check_bounds()` 仅在 `slba + nlb > nsze` 时返回 `NVME_LBA_RANGE | NVME_DNR`；
2. `nvme_post_cqes()` 把 `req->status << 1 | phase` 填入 CQE，并经一次 `pci_dma_write()` 写完整 16-byte CQE；
3. QEMU 从 SQ DMA 读取完整 64-byte command，并把 SQE 的 CID 原样回显到 CQE。

因此 `0x4080` 必然源于某次 QEMU 判定越界的命令。即使 CPU 读到旧 CQE，该旧 CQE 也来自真实的越界完成，不能叫随机 garbage。

## 为什么删除 clflush workaround

- 当前 WSL2 环境没有 `/dev/kvm`，生成的 QEMU 命令不含 `-accel kvm`，实际是 TCG；TCG 不模拟 guest CPU data cache，无法产生题面所述 cache-line stale。
- QEMU 设备模型通过 `pci_dma_read/write()` 访问 guest RAM，不知道 guest 用 4 KiB、2 MiB 还是 1 GiB 页表映射；guest direct-map huge page 本身不会改变 QEMU DMA 地址。
- 若整个 direct-map DMA 真不一致，CPU→设备的 SQ、设备→CPU 的 CQ 与 read data、CPU→设备的 write data 都需要方向正确的 DMA sync。只 flush CQ 既不完整，也可能掩盖 SQ/上游损坏。
- `clflush` 会写回并失效 dirty line；若缺少提交前的 ownership/sync 协议，事后 flush 不是通用修复。

故删除 `CINUX_NVME_WSL2_CACHE_WORKAROUND` 及全部 `clflush`，不进入 uncached DMA 映射重构。

## 新取证设计

### 1. SQ/CQ CID 关联

IO 队列仍由 `io_lock_` 保证一次只有一个 outstanding command，但不再让所有 SQE 永久使用 CID 0：

- 每次提交在锁内分配递增 16-bit CID；
- CQ phase 命中后先检查 controller echo CID；
- CID 不匹配时不消费 CQE，打印一次 raw status、got/expected CID、SQ head/SQ id、driver head/phase，继续等当前 completion；
- status 非零且 CID 匹配时，打印 CQ metadata 与实际写入 SQE 的 opcode/nsid/slba/nlb。

这能区分同 phase 的旧 CQE 与当前命令完成，也避免 stale-success 被静默当成功。

### 2. QEMU 设备侧 LBA trace

QEMU 11.0.2 本机确认支持 `pci_nvme_err_invalid_lba_range`。`QEMU_COMMON_FLAGS` 常开该 error-only event；正常 I/O 零输出，越界时打印：

```text
pci_nvme_err_invalid_lba_range Invalid LBA start=<device_slba> len=<device_nlb> limit=<nsze>
```

它记录 QEMU 已经从 SQE 解码后的值，是判断 SQE 是否在 CPU 日志与设备消费之间改变的关键证据。

## 复现后的判读

| QEMU trace | driver CQ log | 结论 |
|---|---|---|
| start 越界 | CID 匹配，driver SQE slba 合法 | 当前 SQE 的 LBA 字段被改坏；查 SQ owner、写越界、提交/doorbell ordering |
| start 越界 | `CQ CID mismatch` | 旧/错 completion 或 SQ/CQ head 失步 |
| start 与 driver 都越界 | CID 匹配 | 上游 ext2/page-cache 传入 wild block；向上追 block 来源 |
| start 合法仍报 range | CID 匹配 | QEMU/device-model 异常；保存 QEMU 版本与完整 trace 再最小复现 |

证据出来前不做 cache flush、uncached alias 或 DMA 抽象重构。

## 自动门验证

```text
timeout 130 cmake --build build --target run-kernel-test-all -j$(nproc)
```

两 leg 均为 937 passed、0 failed，SMP shootdown IPI test 通过。正常测试未触发
`pci_nvme_err_invalid_lba_range`、CID mismatch 或 NVMe CQ error，符合这些诊断仅在异常路径输出的预期。

## GUI smoke 结果

用户在原触发场景中多轮运行 gcc 编译并持续活动鼠标/光标，未再出现 `status=0x4080`，也未观察到
`CQ CID mismatch`、`CQ error` 或 QEMU invalid-LBA trace。结果支持 CID 关联加固有效，但由于原故障偶发，
不能仅凭“未复现”断言底层原因已经闭环；异常路径诊断继续保留。

若再次出现，保存同一轮中的以下三类输出，不要先加入 cache flush：

```text
pci_nvme_err_invalid_lba_range ...
[NVMe] CQ CID mismatch ...
[NVMe] CQ error ...
```

随后按上文“复现后的判读”表对齐 QEMU 所见 LBA、driver 提交命令及 completion CID，即可继续定位。

## 依据

- [NVM Express Base Specification 2.0d](https://nvmexpress.org/wp-content/uploads/NVM-Express-Base-Specification-2.0d-2024.01.11-Ratified.pdf)
- [QEMU NVMe controller implementation](https://gitlab.com/qemu-project/qemu/-/blob/master/hw/nvme/ctrl.c)
- [Linux DMA API HOWTO](https://www.kernel.org/doc/html/latest/core-api/dma-api-howto.html)
