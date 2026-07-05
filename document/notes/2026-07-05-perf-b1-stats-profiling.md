# 2026-07-05 — B1 gcc 编译卡顿观测设施(PF 计数 + 周期 stats kthread)

## 背景

2026-07-05 用户报 `g++ hello.cpp` 在 CinuxOS 上「卡炸了」(功能对、性能差)。memory
`gcc-compile-stutter-perf` 列疑似根因:page_cache grow-only(批2 无 evict)/ demand paging
风暴 / slab / 调度。方向(memory 铁律 [[debuggability-over-perf]]):**先加观测定位,别盲目优化**。

B1 = 加观测设施 → 跑 g++ 收曲线 → 读趋势定瓶颈。

## 改动(复用 FO `dump_memory_stats` + 加两块)

1. **PF 原子计数器** — `handle_pf` 入口 `__atomic_fetch_add`(IF=0 但 -smp 2 多核并发 PF,plain
   `++` 会 race);accessor `pf_count()` 放 `fault_diag.hpp`(同 PF 诊断族),实现在
   `page_fault.cpp` 紧邻 counter。
2. **diagnostics 扩 PF 行 + delta** — `dump_memory_stats` 加 `[MEM] #PF: N total (+Δ since
   last dump)`,`static last_pf` 跨调用记增量(看**速率**,非单调总量)。
3. **stats kthread** — HPET `g_hpet.monotonic_ns()` 每 1 s 调 dump;§14 双 TU
   (`stats_kthread.cpp` 真实现 + `stats_kthread_stub.cpp` 空 stub),`CINUX_STATS_KTHREAD`
   option 默认 OFF(`cmake/options.cmake`),`kernel/mm/CMakeLists.txt` if/else gate。**源码零
   #ifdef**。PF counter + dump 的 PF 行是无条件编的(panic 也能看),只有周期 thread 被 gate。
4. **`init.cpp`** launch_userspace 前调 `start_stats_thread()`(OFF 时 stub 空)。

## 调试历程(stats kthread 调度,踩了两坑)

- **坑1 — priority 250 + yield 饿死**:`pick_next` 注释「lower runs first」,TaskBuilder 默认
  `priority_=0`,`kernel_init` spawn 没设 → 所有用户进程(/sbin/init/gcc/cc1)继承 priority 0
  (最高档)。stats 设 250 几乎垫底(仅高于 idle 255),gcc 编译 CPU 密集 → stats 永远抢不到
  时间片,deadline 永远到不了,**0 行 dump**(只有 start_stats_thread 的 "started" 那行)。
- **坑2 — priority 0 + sti/hlt 死锁**:提到 band 0(不饿死)但仿 net_poll 用 sti/hlt 等唤醒。
  结果 **gate 卡在 busybox init 第一次 fork**(`[PROC] fork child pid=2` 后无进展),`#PF` 全程
  +0(系统停)。根因:stats 在时间片内 hlt,tick IRQ 唤醒后**继续 stats**(时间片未耗尽),
  init/child 永远 Ready 等 → 饿死。**hlt 在 band-0 线程里拖死所有同级任务**。
- **正解 — priority 0 + yield**(不 hlt):yield 把 CPU 让给下一个 band-0 task(init/cc1),
  PIT tick 再切回 stats;不 halt CPU、不饿死编译。yield 后 init/gcc 跑(~10 ms/tick),stats
  每 tick 醒一次 check HPET(几 ns),1 s 到 dump。**EXIT=0,126 行 dump(31 s × 4 行),gate PASS**。

## 验证

- **默认 OFF**:`run-kernel-test-all` 两 leg **EXIT=0, 2290 PASS / 0 FAIL**(stub 链,零回归;PF
  行/counter 无条件编,`test_memory_stats` 也读到 `[MEM] #PF: 1 total (+1 ...)`)。
- **ON**:`cmake -B build -DCINUX_STATS_KTHREAD=ON -DCINUX_GUI=OFF
  -DCINUX_ROOTFS_PROFILE=buildroot -DCINUX_ROOTFS_BUILDROOT_IMG=build/rootfs-gcc.ext2` +
  `run-buildroot-usability`,g++ 闭环 PASS(`gpp-compile-run`),31 s 曲线。

## 诊断结论(31 s `g++ hello.cpp` 曲线)

```
sec  PMM_free  Cache   PF_total  PF_delta
  0   2088396       3        31  +31       <- boot 早期基线
  7   2087568     125       296  +181
  8   2073884     442      1696  +1400     <- cc1 加载库(demand page 爆发)
 13   2084823     724      4313  +2072
 20   2055062    1522     22882  +18272    <- cc1plus + libstdc++ 加载(峰值)
 25   2081153    1647     30679  +6933
 30   2086187    1767     31117  +7        <- g++ 完成,趋于平静
```

- **PMM free 始终 ~2.08M 页(8GB)充裕**:编译期最低 2055062(sec 20,一瞬 ~130 MB),末尾几乎
  全回收(end=2086187,仅少 2209 页)。**不是内存压力 / OOM**。
- **Cache 仅增长到 1767 页(~7 MB)**:grow-only 确认(批2 follow-up),但 7 MB 相对 8 GB 内存
  微不足道,**不是卡顿主因**(不至于让查找变慢或产生压力)。
- **PF avg 1004/s**:大部分秒 delta 小(+5~+26,正常 fork/exec/demand page);爆发期
  (sec 8/13/20/25,+1400~+18272)对应 cc1/cc1plus/as/ld 加载大库(libstdc++/libc/libm/headers)
  的 demand paging。爆发只占少数几秒。

**结论:gcc/g++ 编译卡顿的瓶颈不在内存子系统**。PMM 充裕、cache 小、PF 适中。卡顿要往别处找:
**QEMU TCG 翻译开销**(host 编 hello.cpp <1 s,QEMU TCG 慢一个量级)/ **disk I/O 延迟**(AHCI
读库 headers)/ **某个热 syscall**(futex/brk/stat)。

memory `gcc-compile-stutter-perf` 假设的 page_cache grow-only / demand paging 风暴**被观测排除**。
避免盲目优化内存 —— 这正是 B1「先加观测定位」的价值。

## 用法

```bash
cmake -B build -DCINUX_STATS_KTHREAD=ON -DCINUX_GUI=OFF \
  -DCINUX_ROOTFS_PROFILE=buildroot \
  -DCINUX_ROOTFS_BUILDROOT_IMG=$PWD/build/rootfs-gcc.ext2
cmake --build build -j$(nproc)
timeout 240 cmake --build build --target run-buildroot-usability 2>&1 | tee /tmp/curve.log
grep '\[MEM\]' /tmp/curve.log   # 曲线
```

## follow-up(B2)

内存非瓶颈,下一步 profiling 转向:

- **syscall 热点**:futex / brk / stat 频率(gcc collect2 fork-exec 链),dump top-N ENOSYS 降噪(接 `gcc-missing-syscalls`)。
- **disk I/O 延迟**:AHCI read latency,cc1 读 .so + headers 的实际 I/O 时间。
- **对照 `-enable-kvm`**:排除 QEMU TCG 翻译开销(host native vs TCG)。
- stats kthread 可选移到 PIT tick handler(IRQ context,彻底不饿死 + 不干扰;当前 yield 版本已够 B1 用,留 B2 若调度精度不够再动)。
