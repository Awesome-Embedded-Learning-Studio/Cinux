# SMAP P0g — path 家族 syscall 分层 + accessor 化

**日期**:2026-06-28　**分支**:feat/f10-tty-dyn
**提交**:8db56c4 (P0g-1) / 3958fbc (P0g-2) / 179d776 (P0g-3)
**验证**:run-kernel-test-all 两 leg 960/0 + ALL PASSED;全量 build + test_host 绿

## 背景:SMAP 重做为什么做这个

F9 批 4 在 syscall_entry + 所有 ISR 入口做了**全局 stac**,让 SMAP 开启后内核仍能解引用用户指针。但 RFLAGS.AC 是 **per-CPU** 位,而 context_switch.S 不存 RFLAGS,任务跨 CPU 迁移后 AC 丢失 → 写已 CoW-resolve 的栈页 → SMAP #PF。`-smp 2` shell 多次 `/hello` 后 `sys_waitpid` 写用户 `*status` 必崩(pte.raw=0xbff19067 证明非页表破坏,是 AC 丢失)。

**修法(对齐 Linux,用户拍板)**:撤全局 stac,改局部 stac/clac(user accessor),所有「内核直接解引用用户指针」迁到 accessor + 分层(`do_*_kernel` 纯 kernel 逻辑 + `sys_*` 薄边界 accessor)。context_switch 不动。

P0a-P0e 已分层 stat / read-write / execve / signal / 杂项家族。**P0g 收尾 path 家族**——最后一个大块裸解引用源头。

## P0g:根除 resolve_user_path 裸解引用

`resolve_user_path`(path_util.cpp)拿到用户 path 后,`path[0] == '\0'` 直接读用户字节,然后 `path_resolve` 遍历整条用户字符串。7 个 path syscall(open/openat/creat/mkdir/chdir/unlink/rmdir)+ stat/newfstatat 尾巴全经它。三步扫清:

### P0g-1(8db56c4):7 syscall 提取 do_*_kernel(纯重构零行为变)
照 P0a `do_stat_kernel` 模式,每个 sys_* 拆出 resolve 之后的纯 VFS 逻辑为 `do_*_kernel(const char* resolved_path, ...)`。sys_* 变成 `resolve_user_path + do_*_kernel`,resolve_user_path 暂不动。test 仍经 sys_* → 旧 resolve_user_path(裸解引用,全局 stac 下能跑),绿。
- do_open_kernel / do_openat_kernel(openat 含 O_CREAT 创建)/ do_creat_kernel / do_mkdir_kernel / do_chdir_kernel(特例:操作 current->cwd,kernel state 非 user 内存,符合 do_*_kernel 定义)/ do_unlink_kernel / do_rmdir_kernel。

### P0g-2(3958fbc):4 test 文件改调 do_*_kernel
test 全传 kernel 栈地址当 path_virt(`reinterpret_cast<uint64_t>(buf)`),P0g-3 后 access_ok 会拒。改:
- 绝对路径:sys_X(addr) → do_X_kernel(buf),删只服务该调用的 addr 声明。
- 相对路径(test_cwd_stat::consecutive_chdir 的 d2):`test_resolve_path`(cwd-aware)+ do_chdir_kernel,语义同原 sys_chdir。
- **边界测试保留 sys_***:test_vfs_syscall 的 sys_open(0)/("")/(bad_addr) + getdents 坏 buf——测的就是 sys_* 边界,P0g-3 后 access_ok 拒 → -EFAULT 符合期望。
- 顺带 vfs_syscall/shell_write 里 sys_write/sys_getdents 的同类 kernel 地址调用一并迁 do_write_kernel/do_getdents_kernel(P0b/P0e 已分层)。

**为何 P0g-2 必须在 P0g-3 前**:resolve_user_path 一旦用 access_ok,所有传 kernel 地址的 test 必挂,所以 test 必须先全改完。P0g-1 的 do_*_kernel 是纯加,可独立绿。

### P0g-3(179d776):resolve_user_path 改 read_user_path accessor
path_util 新增 `read_user_path`(access_ok 预检 + get_user 逐字节到 NUL,照 P0c read_user_string 骨架;拒绝坏地址/空串/超 PATH_MAX)。resolve_user_path 改用它,根除裸解引用。raw path 暂存堆 `PathBuf`(避免 4KB 栈溢出,呼应 kernel/fs/path.hpp PathBuf 教训)。**path 家族 SMAP 真安全**,P3 撤全局 stac 的最后一道大块前置扫清。

## 设计要点
- **分层模式**(对齐 Linux/P0a-P0e):syscall handler 切 `do_*_kernel`(纯 kernel-to-kernel,test/内核直接调)+ `sys_*`(薄边界,accessor 跨用户)。block-then-write 铁律:stac 窗口只在 accessor 内,绝不跨 schedule_blocked。
- read_user_path 照 P0c read_user_string 的 accessor 骨架(同 execve 已验证 work 的模式),不复用其 pool/offset 设计(那是 strvec 用,单条 path 用简洁版)。
- validate_user_ptr 保留(别处 user_ptr.hpp 注释引用),退役但不删(删要改多处注释)。

## 残留(SMAP 重做剩余前置,非本批)
- **P3 撤全局 stac**(SMAP 真生效里程碑):syscall.S:61 stac / :162 clac + interrupts.S 6 处 ISR stac/clac。前提:path 家族 ✅(本批)+ signal_setup_frame(signal.cpp 写用户栈整帧 copy_to_user)+ task_exit_cleartid put_user(sys_exit.cpp:26)+ test_clone 改真 user child_tid + futex WAIT get_user + #PF handler 审计 + **负测试**(SMAP 真开下绕 accessor 应 #PF)+ CR4.SMAP 回读 AP 侧(F9 提 AP 零回读)。
- **P4 清诊断**:工作树 7 个 TEMP 未提交(buddy PMM-RACE 探测器 / process_new CoW-LEAK + cow trace / fault_diag CoW-FAIL walk / fork trace / window_manager_input MouseDown / sys_exit cleartid 注释 / desktop_launch.cpp + log.txt)。
- **生产路径 accessor 盲区**:run-kernel-test 的 test 走 do_*_kernel(不碰 resolve_user_path),只有边界 test 走 sys_*。所以「生产 sys_* 走 accessor 读合法 user path」未被 run-kernel-test 验证(骨架照 P0c 低风险),P3 撤 stac 时 GUI make run 冒烟兜底。

## 关键文件
- kernel/syscall/path_util.{hpp,cpp}:read_user_path + resolve_user_path(accessor)
- kernel/syscall/sys_{open,creat,mkdir,chdir,unlink,rmdir}.{hpp,cpp}:do_*_kernel 分层
- kernel/test/test_{syscall_ext2,vfs_syscall,shell_write,cwd_stat}.cpp:改调 do_*_kernel
