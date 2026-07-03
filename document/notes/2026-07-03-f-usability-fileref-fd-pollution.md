# F-USABILITY: FileRef RAII 根治 double-free + fd 污染治本 + do_read legacy 加固

> 2026-07-03, 分支 `feat/f-usability`。从 busybox echo/cat FAIL(治标 close `7e5f3b6` 临时解)出发,
> 挖出 File 独占语义(double-free)、sys_pipe 漏回滚、do_read fd=0 legacy panic 三个真 bug。
> 用户决策"语法层根治 + 对齐 Linux,不堆 workaround"。批1(FileRef)+ 批3 闭环;批2(共享 offset)
> 留下次。HEAD `8bf1b6d` 未 push。

## 起点:double-free 暴发

批3(治本 fd 污染:sys_pipe 回滚 + socket close + reserve_stdio)踩出 SLAB `[SLAB] double-free` panic。
根因不在治本改动,在 **File 独占所有权语义**:

- FDTable 持裸 `File*`,`FDTable::close`(file.cpp:131)/`~FDTable`(:57)裸 `delete`;`dup`/`dup2`/fork/clone `new File(...)` 独立拷贝。
- `test_sys_pipe_set_*` 的 `set()` 后手动 `delete retrieved` 与 `~FDTable` 析构 `delete` 冲突 → double-free(test_sys_pipe.cpp ~15 处)。既有 bug,`reserve_stdio`(3 个占位 File)挪了 SLAB 布局让它从"静默"变"panic"。

## 批1:FileRef RAII(commit `6d21066`)—— 语法层根治 double-free

**设计**(照搬 inode DEBT-023 范式,**无 friend**):
- `File` 加 `uint32_t refcount{0}`(public,atomic)。File 仍 trivial dtor(不碰 inode —— 保 file.hpp:75-77 测试 fixture flexibility:栈 Inode 析构顺序不 UAF)。
- `FileRef` 值类型 RAII:copy `+ref`(共享 File = Linux open-file-description 语义)、析构 `-ref` 到 0 才 `delete`。`get()/->/operator bool`。
- `FDTable::fds_[]` 改 `FileRef`(值语义)。`set` displaced 锁外 unref(守 DEBT-023 锁外 release);`close` move out + 锁外 unref;`~FDTable = default`(FileRef 自动)。
- inode refcount **留 FDTable**(alloc/set +1,close -1,DEBT-023 不变)—— 行为零变化。把 inode_unref 收进 ~File 是"inode ref 随 File"(共享 File 前提),但破坏栈 Inode 析构顺序,**留批2**(那时测试栈 Inode 改堆一并做)。

**改动**:file.hpp(File refcount + FileRef 类 + FDTable 持 FileRef)+ file.cpp(alloc/set/get/close/dup/dup2/~FDTable 用 FileRef)+ test_sys_pipe.cpp 删 15 处裸 `delete`(kernel)+ test/unit/test_sys_pipe.cpp 删 `delete f1`(host)。

**验证**:两 leg 1108/0 + busybox 14/14 + host test_host 69/69 + 无 panic(double-free 仅剩 `test_slab_doublefree::test_double_free_detected` 的预期检测日志)。

## 批3:fd 污染治本 + do_read legacy panic(commit `8bf1b6d`)

三个真 bug:

1. **`sys_pipe` 失败路径漏回滚**(sys_pipe.cpp:76-78):`do_pipe_kernel` 已 alloc 2 fd,`copy_to_user` 失败直接 `return -kEfault` 没 close → fd 泄漏。**生产也漏**(pipe() 失败 leak fd)。修:`tbl.close(pipefd[0]); tbl.close(pipefd[1]);` 回滚。
2. **`test_socket_dgram/stream_returns_fd` 漏 close**:socket() 成功 fd 断言后没 close → 污染共享 fd 表。修:`current_fd_table().close(fd)`。
3. **`do_read fd=0` legacy console_tty NotNull-panic**(sys_read.cpp:62-65):`console_tty().read` 经 `prepare_to_wait` 需 current task,测试内核 ring-0 主线程 `current()==null`,`close+read(fd=0)` 落此路径 NotNull-panic。修:`if (Scheduler::current()==nullptr) return -kEbadf;` short-circuit。**真 bug**(NotNull 不该因测试无 current 触发)。

**诊断深挖(交接要点)**:
- 批3 路上试过 `reserve_stdio`(占位 fd 0/1/2 nullptr-inode File 替代治标 close)。踩坑链:① reserve_stdio 在 kernel_main 太早(slab init 前)→ `new File` 返 null → 占位失败(test do_open 仍拿 fd 0)。移到 slab init 后占位 OK。② 但**测试 close(0/1/2) 释放占位**(reserve_stdio 占位可 close)+ 后续 do_open 拿 fd 0 → `do_read(0)` after close 落 console_tty NotNull-panic。
- batch1 没踩 double-free/panic 只是**偶然**:pipe 测试漏 close 泄漏 fd 0/1 当了"盾",test do_open 拿 fd≥2,do_read(fd≥2) after close → -EBADF(非 fd==0 legacy)。sys_pipe 回滚(批3)修了泄漏 → 盾没了 → 暴露 do_read fd=0 地雷。
- do_read fd=0 加固(批3)后,sys_pipe 回滚 + socket close 安全(不 panic)。

**保留 workaround**:`main_test` smoke entry 的治标 close(`7e5f3b6`)—— fd 0/1/2 污染(echo fflush EINVAL)仍需它清(批3 修污染源 sys_pipe/socket,但别的测试可能再漏 close)。删它需 reserve_stdio/legacy 根治(follow-up)。

**验证**:两 leg 1108/0 + busybox 14/14 + host 69/69 + 无 panic。

## 当前状态(HEAD `8bf1b6d`,未 push)

- 治标 close(7e5f3b6,main_test smoke entry)+ 批1 FileRef(6d21066)+ 批3(8bf1b6d)。
- busybox echo/cat PASS(治标 close 清 fd 污染 + 批1 防 double-free + 批3 修污染源 + do_read 加固)。
- CI 5 job 未 push 验(待用户 push)。

## Follow-up(下次 / 独立批)

### 批2:dup/fork/clone 改共享 File(对齐 Linux offset)—— memory `f-eco-b3-b4`
- `file.cpp dup/dup2` + `fork.cpp:431` + `clone.cpp:319`:`FileRef(new File(src->...))` → `fds_[new] = fds_[old]`(copy +ref 共享)+ `inode_ref(src->inode)` 配对(close 时 file_unref + inode_unref)。
- 验证 `test_sys_pipe_dup_last_close_eof`(199)仍绿(测 inode refcount,与 File 共享正交 —— dup 让两 fd 共享 File/同 inode,inode ref=1;close → file_unref 不删 File,inode ref 不触 0;末次 close 删 File → inode_unref → EOF)。
- 新增"dup 后两 fd 共享 offset"对齐测试。
- **inode ref 进 File**(共享 File 前提):File ctor `inode_ref`、~File `inode_unref`,FDTable 不再 inode_ref/unref。破坏栈 Inode 析构 → **测试栈 Inode 改堆**(test_sys_pipe ~21 处)。
- 风险:offset 共享行为变(CinuxOS 无用例依赖 fork/dup 独立 offset,fork→execve 为主,strict 改善)。

### reserve_stdio / legacy fd=0/1/2 根治(删治标 close)
- do_read fd=0 加固(批3)防 panic,但 fd 污染(echo fflush)仍需治标 close。
- 删治标 close 需重构测试内核 stdio 模型:去 do_read/do_write 的 legacy fd=0/1/2 路径(fd 0/1/2 当普通 fd,nullptr→-EBADF),或测试内核 boot 开真 /dev/console inode 占 0/1/2。
- plan: `~/.claude/plans/sunny-hopping-journal.md`。

## 教训
- **File 独占语义是 double-free 源**;RAII(FileRef)语法层根治(refcount 配对由类型系统托底,不靠纪律)。
- **friend 不是唯一解**:FileRef + refcount public(对齐 inode 范式)更干净,用户明确否决 friend。
- **do_read fd=0 legacy + 测试内核 current null 是地雷**:NotNull 不该因测试无 current 触发,null short-circuit 是合理 legacy 加固。
- **诊断顺序**:double-free(SLAB 检测)→ FileRef 根治(批1)→ fd 污染(治标 close + 污染源 sys_pipe/socket 修,批3)→ do_read panic(legacy 加固,批3)。每步绿才下一步。
- **"盾"现象**:batch1 绿部分因 pipe 泄漏 fd 0/1 当盾(避 test do_open 拿 fd 0)。修泄漏(批3)暴露地雷。诊断时警惕"绿≠对"。

接 memory `f-usability-ci-busybox-followup` / `f-usability-symlink-follow`。
