# 【回补】2026-06-29 修两条 CI 红线 — scheduler.cpp 500 行拆分 + forktest.c C99

**日期**:2026-06-29　**分支**:fix/smap_bug_fix
**提交**:8d9a2f9
**验证**:`check_line_limits` OK、`build-forktest` C99 编过、`run-kernel-test-all` 两 leg 绿

## 背景

`24c4559`(2026-06-29 那篇 `-smp2` fork exit/reap 复活 saga)在本分支落地后,两条早就定下的 CI 红线在本分支干净构建时被触发。本地一直没炸是因为 musl sysroot 和 forktest 二进制都被缓存着没重编,scheduler.cpp 也没人再改过行数;CI 是干净环境,从头重编,两条红线一起亮。这是小修,不涉及行为,但两条都值得记一下根因——尤其 500 行软限那条,是项目硬规矩。

## 红线 1:scheduler.cpp 超 500 行软限(拆分)

### 根因

CinuxOS 源文件有 **500 行软上限**,`scripts/check_line_limits.py` 在 CI 里门禁 `kernel/` 源文件(test/ 与 mini/ 豁免)。这条规矩来自 2026-06-18 F3-M2 的 PR review,用户原话是「防止超大行文件的出现,造成阅读疲惫」——一个文件一个聚焦职责,超了就按职责拆,别等 PR 被打回。

`24c4559` 给 `Scheduler::init()` 加了「为 online AP 重建 idle」的逻辑(本分支那条 saga 的核心修复),把 [kernel/proc/scheduler.cpp](../../kernel/proc/scheduler.cpp) 撑到了 **518 行**,过线。

### 实现

按职责把**等待/阻塞机制**整组抽到新文件 [kernel/proc/scheduler_block.cpp](../../kernel/proc/scheduler_block.cpp)(90 行):

- `block()` / `unblock()`:直接阻塞/唤醒,管理 + 测试驱动路径用。
- `prepare_to_wait()` + `schedule_blocked()`:wait 路径专用的「丢唤醒安全」配对,关跨 CPU lost-wakeup 窗口。

这几块本身就是一组(都围绕「任务怎么停下来等、怎么再跑起来」),抽出去后主文件 [scheduler.cpp](../../kernel/proc/scheduler.cpp) 回到 **449 行**,核心的 `schedule()` / `exit_current()` / `idle` 路径全留下不动。三个函数都是 `Scheduler` 的成员,挪文件只是改位置、加 include([kernel/arch/x86_64/smp.hpp](../../kernel/arch/x86_64/smp.hpp) 给 `unblock` 里的 `arch::wake_idle_ap`)、在 `CMakeLists.txt` 登记新源文件,**零行为变化**。原位置留一行注释指明去向。

为什么挑这一组拆、不拆别的:`schedule()` 是调度器的心脏,跟 idle/exit_current 强耦合,拆出去反而割裂;而 block/unblock/prepare_to_wait/schedule_blocked 是相对自洽的「睡眠/唤醒」原语,分出去正好各自聚焦。这呼应 CODING-TASTE 的拆分判据——按职责、不按行数硬切。

## 红线 2:forktest.c 数字分隔符不是 C99

### 根因

[tools/musl/forktest.c:118](../../tools/musl/forktest.c) 原来写的是

```c
for (int spins = 0; spins < 2'000'000; spins++) {
```

`2'000'000` 这种单引号数字分隔符是 **C++14** 才有的语法。但 forktest.c 是个 `.c` 文件,musl 那套工具链用 `gcc -std=c99` 编它——C99 不认这个分隔符,会把 `'000'` 当成多字符常量(`'000'` ≡ 0x303030),报错。

本地漏过的原因:forktest 的 musl sysroot 和产物被缓存了,`build-forktest` 没真重编,旧二进制还在,于是本地一路绿;CI 是干净构建,从头编这个 `.c`,立即炸。**这类「缓存掩盖、CI 暴露」的红线,提醒改 musl 侧 `.c` 文件后要清干净构建验证,别只看本地增量绿。**

### 实现

把 `2'000'000` 改回 `2000000`。纯字面量修正,行为不变(还是两百万次自旋上限,配合 WNOHANG 轮询 reap child)。forktest 这段本来是 `-smp2` smoke 里 reap child 的轮询循环(`24c4559` saga 的同一片代码),C99 编过才能给后续 `run-kernel-test-all` 的 `-smp2` forktest leg 铺路。

## 验证

- `check_line_limits`:scheduler.cpp 回到 449,过线。
- `build-forktest`:C99 模式编过,无多字符常量报错。
- `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)`:两 leg(单核 + `-smp2`)绿,`hello 20/20` + `forktest` PASS。

## 关键教训

- **缓存掩盖 CI 红线**:本地增量构建常带缓存(musl sysroot / 已编二进制),一些「编译期就错」的问题(C99 分隔符、行数门禁)在本地绿、CI 红。改 musl 侧源文件或碰过行数的源文件,本地清干净构建再认绿,别只信增量。
- **500 行软限是拆分判据不是数字游戏**:拆哪一组看职责自洽(block/unblock 是一组睡眠/唤醒原语),不是机械地按行数切。`schedule()` 这种核心即使想动也割不开,优先拆原语组。

## 残留

无。本批纯修两条 CI 红线,不引入新行为、不欠新债。
