# NVMe io_submit yield 探索与回退(为何不能持锁 yield)

> 日期：2026-07-11 · commits `c3dca73` → `3c15fed`(revert) → `83ec5a2`(注释) + `10786b5`/`93e2b55`(host poll)
> · v1.0.0 发版后真实可用化 · 状态：✅ 合 main(探索失败，回退，留 v1.1 异步 IO)

## 背景

NVMe IO 是同步 poll（`io_submit` 轮询 CQ 到 completion 才返回）。gcc 编译时别的 shell 卡——poll 占满
CPU。本想给 `io_submit` 的 poll 循环加 `yield()` 让出 CPU。

## 探索（commit c3dca73）

`io_submit` poll 循环加 yield：每轮询一轮没命中就 `yield()`，让 gcc 编译时别的 shell 不被饿死。

## 翻车（lockdep panic）

`io_submit` 触发 lockdep panic：`NvmeBlockDevice::read_blocks` 持 `lock_` across NVMe IO（SMP 序列化
`dma_buf_`，见 memory `749e7db`），`io_submit` 在锁内被调，yield → `schedule()` 时持有
`NvmeBlockDevice::lock_`，违反**「不持 spinlock 跨 yield」铁律** → lockdep panic。backtrace：

```
handle_pf → PageCache::get_page → Ext2 read → NvmeBlockDevice::read_blocks
→ io_submit(yield) → schedule() panic
```

## 回退（commit 3c15fed）

回退 yield（`io_submit` 恢复同步 poll，手动锁保留无 yield 安全；mismatch advance 保留无害）。NVMe IO
饿死（gcc 编译时别的 shell 卡）回退为**已知问题**，根治要 NVMe 异步 IO（blocking + wait queue），留
v1.1+。

commit `10786b5`（host pump poll 改 10ms）保留——它是纯 host 用户态，不碰内核锁，确实释放了 idle CPU。
后 `93e2b55` 又 revert 了 host pump poll 10ms（没解决 gcc 饿死 + 加输入延迟）。

## 注释订正（commit 83ec5a2）

revert 后 `io_submit` 开头注释仍提 yield（误导）。改成解释：`io_submit` 在 `NvmeBlockDevice::lock_`
内被调，**不能 yield / schedule**（lockdep 「spinlock held across schedule」panic）；同步 poll 的饿死
问题治本是**异步 IO**（blocking + wait queue，v1.1+），不是持锁 yield。

## 验证

回退后 run-kernel-test-all 两 leg 绿。

## 教训

- **持 spinlock 时不能 yield / schedule**——这是 lockdep 守的铁律，违反即 panic。poll 路径若在锁内，
  让 CPU 的唯一正解是异步化（completion 后唤醒 wait queue），不是持锁 yield。
- 「让出 CPU」的直觉解法在持锁路径上是陷阱；探索性改动用 lockdep 矩阵兜底，翻车就干净回退 + 留注释
  解释为什么不行，别留半截。
