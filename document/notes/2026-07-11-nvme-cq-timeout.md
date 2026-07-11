# NVMe CQ 轮询超时修复

> 日期：2026-07-11 · PR #77 远程 CI Debug+lockdep SMP 失败 · 状态：本地门禁绿，待远程矩阵复验

## 现象与定性

GitHub Actions run `29133259819` 的 11 个 job 中仅 `kernel-tests (Debug, lockdep)` 失败。该 job 的单核腿
934/934 全绿，SMP 腿在 NVMe 首次 NVM write 失败：

```text
[NVMe] IO queues created (qid=1 size=64)
[FAIL] (ctrl.write_blocks(1, 0, 1, wbuf).ok()) == true at kernel/test/test_nvme.cpp:104
=== Tests: 933 passed, 1 failed ===
```

失败前没有 `CQ error`、`CQ CID mismatch` 或 QEMU `pci_nvme_err_invalid_lba_range`，因此不是新的
`0x4080 = DNR | LBA_RANGE` 证据。`nvm_io()` 在该位置唯一不打印日志的失败路径是 `io_submit()` 轮询
`1,000,000` 次后返回 `Error::TimedOut`。用户多次复验后估计失败率约 80%，已不能当作 runner 噪声。

## 根因

IO CQ 等待把循环次数误当作时间预算。Debug、lockdep、TCG 与 SMP 会改变一百万次 guest 循环对应的
实际时间，以及 QEMU 设备模型获得调度的时点；因此同一逻辑在 Release 或单核能过，在最慢矩阵中可能在
completion 可见前耗尽预算。

这是驱动可靠性问题：外部调度触发抖动，但固定迭代预算使抖动成为测试失败。

## 修复设计

1. HPET 可用时以 `monotonic_ns() + 500 ms` 为真实 deadline，不再按 CPU 执行速度判超时；HPET 在生产和
   测试启动序中均早于 NVMe。
2. 每次未命中 completion 后执行 `pause`，降低紧轮询压力。
3. HPET 不可用时保留原一百万次循环作为早期启动兜底，不引入无限等待。
4. timeout 打印当前 CQ raw status/CID/SQ head/SQ id、提交 CID、driver SQ tail/CQ head/phase 及命令
   opcode/nsid/slba/nlb。即使仍失败，也能区分 completion 未出现、旧 CQE 与队列失步。
5. 将 NVM IO 构造、提交和 CQ 轮询从正好 500 行的 `nvme.cpp` 拆到 `nvme_io.cpp`；公共接口与队列布局
   不变。

范围不包含 NVMe 中断化、DMA cache 属性调整、queue depth 扩展或 `0x4080` 根因推断。

## 风险审查

- `io_lock_` 的 owner 和同步协议不变，仍保证同一时间仅一个 outstanding IO command。
- deadline 读取 HPET MMIO，不依赖 IRQ，适用于当前持自旋锁轮询上下文。
- 正常 completion、CID 校验、phase 翻转和 CQ doorbell 顺序未改。
- 500 ms 仅是异常上限；正常 QEMU completion 仍立即返回。

## 验证

```text
cmake --build build --target big_kernel_test -j$(nproc)  # PASS
timeout 60 cmake --build build --target run-kernel-test -j$(nproc)
```

结果：934 passed、0 failed；NVMe read/write round-trip 通过，无 CQ timeout、CID mismatch 或非零 status。
本地 build 为无优化 + `CINUX_LOCKDEP=ON`。按仓库顶层正式验证约束本批只运行 `run-kernel-test`，远程
`Debug + lockdep + -smp 2` 矩阵仍是合入前的最终复验项。
