# F-DYN-COV 批2 — inode_cache_ RACE_TOUCH 验证靶子 + 补批1 list 遗漏

**日期**：2026-07-08
**分支**：feat/boost_cinux（批2，未 push）
**前序**：批1（f150c78）race-detect 基建。

## 补批1 遗漏（关键）

批1 commit f150c78 漏把 `RACE_DETECT` 加进 `CINUX_COMPILE_DEF_OPTS` list（只加了 option 段）。导致 [kernel/CMakeLists.txt:131-133](kernel/CMakeLists.txt#L131-L133) foreach 不加 `-DCINUX_RACE_DETECT` compile def → `RACE_TOUCH`/`lockdep_assert_held` 宏一直 no-op（`#ifdef CINUX_RACE_DETECT` off）。

**误判更正**：批1 note 说「机制测试 -smp2 PASS」——实际是第一次验证（#ifdef 还没加，机制测试段总编译，RACE_DETECT=ON 链 race_detect.cpp probe 有实现，PASS）。加 #ifdef 后 def 没传，段跳过，机制测试不跑（L7154 ALL PASSED 是其他 test PASS，机制测试跳过）。**批1 验证有假象**。

批2 list 加 RACE_DETECT 后，def 传（`CXX_DEFINES = -DCINUX_LOCKDEP -DCINUX_NET -DCINUX_RACE_DETECT -DCINUX_USB -DCINUX_VIRTIO`），#ifdef 生效，机制测试真 PASS（L7153 `[F-DYN-COV] race-detect test: PASS (detected cross-CPU)`）。

## 批2 代码：inode_cache_ 验证靶子

[ext2_inode.cpp](kernel/fs/ext2/ext2_inode.cpp) `get_cached_inode` 入口加 `RACE_TOUCH(g_inode_cache_wp)`（`#ifdef CINUX_RACE_DETECT` 包声明）。标 inode_cache_ 无锁共享，production -smp2 两 CPU 并发访问触发 `[SMP-RACE]` kpanic。

**不修 race**（批3 加 `inode_cache_lock_` 闭环）。

## 验证

- ON build -smp2 leg：机制测试真 PASS（L7153）+ ext2 test 不误触发（BSP 单 CPU 访问 inode cache，watchpoint `last_cpu=BSP` 不跨 CPU，ALL PASSED）。
- OFF build 编译过（`#ifdef` 跳过 `g_inode_cache_wp` 声明 + RACE_TOUCH no-op，零行为改变）。
- GOTCHA：OFF build `g_inode_cache_wp` 声明要 `#ifdef` 包（否则 RACE_TOUCH no-op 不引用 → `unused-variable` -Werror）。

## 待用户验证（production GUI）

用户启 GUI -smp2 跑 gcc（build/ RACE_DETECT=ON）：两 CPU 并发 ext2 inode cache → 期望 `[SMP-RACE] ext2.inode_cache: cpuN touched after cpuM without lock` + 凶手栈（backtrace 指向 `get_cached_inode`）。**抓到即工具有效**，批3 修 race。
