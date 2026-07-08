# VFS offset_lock_ schedule-while-held 治(LOCKDEP 抓)

**日期**:2026-07-08
**分支**:feat/boost_cinux(未 push)
**前序**:批3 开 CINUX_LOCKDEP=ON(build/) → 用户 GUI -smp2 跑 smoke → LOCKDEP kpanic `schedule() called with 1 spinlock(s) held` 凶手栈 `schedule <- schedule_blocked <- PtySlaveOps::read <- do_read_kernel <- sys_read`。

## 根因

[sys_read.cpp:43](kernel/syscall/sys_read.cpp#L43) do_read_kernel 持 `file->offset_lock_.guard()` 调 `ops->read()`。pipe/pty read 调 `schedule_blocked`(等数据) → 持 offset_lock_ 阻塞 schedule = 死锁风险(他核 read/write 同 file 等 offset_lock_ 卡死)。

do_write_kernel 同 bug(L48 持 offset_lock_ 调 write,pipe write 可能阻塞 if full)。

PtySlaveOps::read 自己释了 slot->lock(L219 guard 析构),但 do_read_kernel 的 offset_lock_ 还持。LOCKDEP 之前 GUI build 没开没抓到,批3 开 LOCKDEP 抓到——**正是 LOCKDEP 该抓的**。

## 修法

`is_page_cacheable()` 分支(disk,read_bytes 不阻塞 schedule)持 offset_lock_ + 更新 offset。非 cacheable(pipe/pty/ramdisk)不持锁,unlocked 更新 offset(单 fd read 常见,dup 竞态少见可接受)。

Ramdisk 非 cacheable 但有 offset(seek),仍更新 offset(test_vfs_syscall read offset test 验)。

## 验证

- 单核 leg ALL TESTS PASSED(read/write/pipe test 过,含 test_vfs_syscall L295/L316/L388 offset test + test_sys_pipe_sigpipe)。
- -smp2 leg pipe test PASS(L907-916)。shootdown IPI 偶发卡(memory -smp 已知,非本 fix)。
- production -smp2 gcc smoke 待用户验(pty read 阻塞,LOCKDEP 不再报 schedule-while-held)。

## GOTCHA

- `is_page_cacheable()` 不区分「有 offset」vs「无 offset」:Ramdisk 非 cacheable 但有 offset(seek),pipe/pty 非 cacheable 无 offset(流式)。修法:非 cacheable 都 unlocked 更新 offset(pipe/pty 无害,Ramdisk 正确)。
- LOCKDEP per-CPU held 栈跨 CPU 迁移残留(memory GOTCHA)。但本 bug 是真持锁 schedule,非残留。
