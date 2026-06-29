# 【回补】2026-06-28 SMAP M0-P0e — user accessor 框架 + 五大 syscall 家族分层(重做地基)

**日期**:2026-06-28　**分支**:feat/f10-tty-dyn
**提交**:`80d5daf`(M0)/ `227ee95`(P0)/ `2b18101`(P0a)/ `f6f0f43`(P0b)/ `6e88fc7`(P0c)/ `e9cff8e`(P0d)/ `42d5640`(P0e)
**验证**:run-kernel-test-all 两 leg(单核 + `-smp2`)逐批绿,基线 960/0

> 这是 SMAP 重做(P0a-P0g + P3 + (B) + P4)**全部建在上面的地基**。当时的
> follow-up note 只写了收尾的 P0g path 家族([2026-06-28-smap-p0g-path-family.md](2026-06-28-smap-p0g-path-family.md)),
> 这篇把更早、也更根本的 M0 accessor 框架 + P0a-P0e 五大家族分层补上。

## 背景:为什么 SMAP 要推倒重来

F9 批 4(SMEP/SMAP 那一批)开了 SMAP,但为了让内核能继续解引用用户指针,它在
`syscall_entry` 和**所有 ISR 入口**做了**全局 `stac`**——一进内核就把 RFLAGS.AC
拉高,等于整段内核态都放行用户内存访问。这是错的,因为:

- **RFLAGS.AC 是 per-CPU 位**,而 [context_switch.S 不存 RFLAGS](../../kernel/arch/x86_64/context_switch.S)。
- 任务从 CPU0 迁到 CPU1 后,新 CPU 上的 AC 是旧值(可能 0),但代码还在按「全局 stac 已开」
  的旧假设裸解引用用户指针。
- `-smp 2` shell 反复 `/hello` 后,`sys_waitpid` 写用户 `*status` 正好撞上 AC=0 的 CPU → SMAP #PF。

**修法(对齐 Linux,用户拍板)**:撤掉全局 stac,改成**局部 `stac`/`clac`** 的 user
accessor——只在真正跨用户/内核的那一小段拷贝窗口里放行 AC,拷完立刻关掉。然后把所有
「内核直接解引用用户指针」的路径迁到 accessor 上,并给 syscall handler 做**分层**:
纯内核逻辑(`do_*_kernel`)和跨用户边界(`sys_*`)切开。

这篇记的就是这个地基的两件事:**accessor 框架怎么设计的**,以及 **5 个 syscall 家族怎么分层**。

## M0(80d5daf):user accessor 框架

新建 [kernel/arch/x86_64/user_access.hpp](../../kernel/arch/x86_64/user_access.hpp),对齐
Linux `uaccess.h`。两个 namespace:

- `cinux::arch::stac()` / `clac()`——就是裸的 `stac`/`clac` 指令(host 单测下 no-op,
  保持 RFLAGS 干净)。
- `cinux::user::`——`access_ok` / `copy_to_user` / `copy_from_user` / `put_user` / `get_user`。

### accessor 的窗口铁律

整个框架最关键的一句话,写在头注释里:**「accessor 窗口绝不阻塞/调度」**。

理由就是上面的根因:AC 是 per-CPU 位,context_switch 不存 RFLAGS。如果 `stac` 之后、
`clac` 之前你 `schedule_blocked` 了,任务被切走又可能在 AC=0 的 CPU 上被切回来,等于
带着放行态裸解引用用户内存。所以每个 accessor 都是 `stac → 紧字节循环拷贝 → clac`,
拷贝过程不碰任何会阻塞的东西。

**推论(block-then-write 铁律)**:凡是会阻塞的 syscall(read 等 stdin、waitpid 等子进程),
不能边阻塞边碰用户内存。正解是先 block 在**内核缓冲区**上,等重新 runnable 了,再用
accessor 把内核缓冲 copy_to_user。这个铁律贯穿 P0/P0b,也是后面 P3 撤全局 stac 的命门。

### access_ok:比 validate_user_ptr 严格在哪

`access_ok(addr, size)` 是**纯范围检查**(不缺页、不走页表),规则:`addr` 和 `addr+size-1`
都要落在用户 canonical 半区(bit 47 = 0);NULL 拒、回绕拒(`__builtin_add_overflow`)。

旧的 `cinux::syscall::validate_user_ptr` 只查 canonical 形式,**会放过内核高半地址**——
这是它的缺陷,M0 顺手修正。access_ok 把内核高半、NULL、回绕全挡掉,是后面所有 sys_*
边界 accessor 的第一道门。

### 容错契约(暂时没有 exception table)

CinuxOS **没有** Linux 那种 `_ASM_EXTABLE_UA` + `copy_to_user` 截断返 `-EFAULT` 的
exception table 基建(见 memory 里 follow-up #2)。所以这些 accessor 的容错靠两条:

1. `access_ok` 预先把坏范围挡掉,accessor 返 `false`,调用方返 `-EFAULT`。
2. 拷贝中途遇到 not-present 用户页,靠 #PF handler 的 demand-paging 兜(AC 在窗口内是 1,
   缺页能被正常服务);只有真正不可映射的地址才会 SIGSEGV/panic。

这是个**临时的妥协**——等 exception table 基建起来,accessor 才能像 Linux 那样对真 fault
截断返 `-EFAULT`,而不是靠 PF handler。但当时这一层够用,先把分层做起来。

### M0 测试

扩 `test_user_ptr` 测 access_ok 的边界(接受合法 user / 拒内核高半 / 拒 NULL / 拒回绕)
和 copy/put/get 的「access_ok 拒 → 返 false」路径。`+5 新测,零回归`。

## P0(227ee95):先用 accessor 治住冒烟的 waitpid #PF

M0 框架就绪后,**先打一发实战**:把 `sys_waitpid` 写用户 `*status` 改成 `put_user`。
这是当时 `-smp2` shell `/hello` 必崩的现场,也是验证「局部 stac 取代全局 stac」思路能跑通的最小证据。

做法([kernel/syscall/sys_waitpid.cpp](../../kernel/syscall/sys_waitpid.cpp)):`waitpid()` 本身
是个内核函数,把 exit 状态写进**内核出参** `kstatus`;只有 `sys_waitpid` 这个 syscall
边界才跨用户内存。所以 sys_waitpid 里先 `waitpid(&kstatus)`,再 `put_user(kstatus, status)`
——**`put_user` 在写 `*status` 那一刻自己 stac**,完全不依赖 syscall_entry 的全局 stac,
根治 AC 跨核丢失的 #PF。失败返 `-EFAULT`,且 `waitpid()` 签名不变(test 直接调传 kernel
地址不受 accessor 影响)。

这一批也顺带定下后续策略:stat/pipe/clock 等其余写用户的 syscall**暂缓**迁移 accessor,
因为——

> test 直接调 handler 传 kernel 栈地址,与 access_ok 拒 kernel 地址冲突 —— 需 test 基建
> (user 地址)或 handler 分层,后续决策。

这就直接催生了 P0a-P0e 的分层:与其给每个 handler 配一套「user 地址测试基建」,不如照
Linux 做 `do_*_kernel` / `sys_*` 分层——test 改调纯内核的 `do_*_kernel`(传 kernel 地址,
不碰 accessor),边界 accessor 留给真用户态路径。

## P0a-P0e:五大 syscall 家族分层

分层模式是统一的(对齐 Linux),每个家族批都照同一套:

- **`do_*_kernel(...)`**:纯 kernel-to-kernel 逻辑。收 kernel 指针/buf,**可以 block、
  可以 unmap 旧用户页、可以持有任何内核状态**,因为它根本不碰用户内存。test 和内核内部直接调。
- **`sys_*(...syscall 参数)`**:薄边界。先用 accessor 把用户参数(path、buf、argv…)
  搬进内核暂存(小放栈、大放堆 `kmalloc`),再调 `do_*_kernel`。
- **block-then-write 铁律**:凡 block 的家族,block 发生在 `do_*_kernel` 写内核 buf 时
  (AC=0 安全),block 回来后再 `copy_to_user`——stac 窗口绝不跨 schedule。
- **test 改调 `do_*_kernel`**:happy path 传 kernel 地址;边界 case(NULL buf / bad fd /
  noncanonical addr)保留 `sys_*`,因为测的就是 access_ok 拒 → `-EFAULT`。

### P0a(2b18101):stat 家族

`sys_stat` / `sys_fstat` / `sys_newfstatat` 切两层:

- `do_stat_kernel(resolved_path, kst)` / `do_fstat_kernel(fd, kst)`:`vfs_resolve` + lookup +
  stat 的纯内核逻辑,写 kernel `stat`。
- `sys_*` 边界:`resolve_user_path → do_*_kernel → copy_to_user`。stat 输出缓冲是**唯一**
  用户指针,经 `access_ok` + stac 拷出。**删掉 `fill_user_stat`**(裸 memcpy 用户内存,
  全局 stac 假设)。

[test_cwd_stat](../../kernel/test/test_cwd_stat.cpp) 的 sys_stat/sys_fstat 改调 do_*,并加了
`test_resolve_path` helper(cwd-aware 解析 kernel path),躲 access_ok 拒 kernel 地址。

**这里埋了个伏笔**(显式写在 commit message 里):path_util 的 `read_user_path`(根除
path 裸解引用)留到 path 家族批——改它会破 creat/open/mkdir/chdir/unlink/rmdir 的 test
setup,必须一起分层。这就是后来 P0g 三步走的源头(见 [P0g note](2026-06-28-smap-p0g-path-family.md))。

### P0b(f6f0f43):read/write 家族(block-then-write 的样板)

`sys_read` / `sys_write` / `sys_readv` / `sys_writev` 切两层:

- `do_read_kernel(fd, kbuf)` / `do_write_kernel(fd, kbuf)`:纯 kernel-to-kernel。
  fd=0 时 `do_read` 调 `console_tty_read(kbuf)`——**block 在 kernel buf 上(AC=0 安全)**,
  这是 block-then-write 铁律的标准样板;fd=1 时 `do_write` 的 `kprintf` 读 kernel buf
  (删掉裸 `buf[i]`)。
- `sys_*` 边界:`access_ok` + 内核暂存(`kmalloc`,小栈大堆防爆栈)+ `copy_from/to_user`。
  stac 窗口只在 accessor 内,**绝不跨 `schedule_blocked`**——P3 撤全局 stac 的命门。
- `readv/writev`:`copy_from_user` 分块读 user iovec(`kIovChunk=8` 防爆栈),逐段委托
  **已分层的** `sys_read`/`sys_write`(复用,不重写)。

test 大面积改调 do_*(cwd_stat/shell_write/syscall/shell/vfs_syscall/syscall_ext2 的 happy path),
边界 case 保留 sys_*。test 里残留几个改 do_* 后没用的 `X_addr` 变量(非 -Werror,不阻塞,
留后续清)。

### P0c(6e88fc7):execve 家族(两层 accessor 的样板)

`sys_execve` 切两层,这一批的精华在 `copy_strvec` 的**两层 accessor**:

- `do_execve_kernel(kpath, kargv, kenvp)`:纯 kernel-to-kernel(proc::execve + 进 user mode)。
  收 **KERNEL 字符串**,所以**可以 unmap 旧用户页 / 可以 block on VFS,而不持任何用户指针**
  ——execve 这种会摧毁调用方地址空间的操作,这是关键安全性质。test/内核直接调。
- `sys_execve` 边界:accessor 先把 path/argv/envp 搬进内核 pool(堆,避栈压),才调 do_*。

`copy_strvec` 为什么是**两层** accessor——因为 argv/envp 是「指针的数组,每项又指向字符串」,
两种 user 内存要分别搬:

1. `get_user` 读**指针槽**(每项 `const char*`,8 字节),拿到用户字符串地址;
2. 再 `get_user` **逐字节**读该字符串到 NUL(`read_user_string`)。

`read_user_string`(读单条 user path)也在这批定型——`access_ok` 预检 + `get_user` 逐字节。
这套骨架后来被 P0g 的 `read_user_path` 照搬(同模式,单条 path 用简洁版,不复用 pool/offset)。

**根除的裸解引用**:`user[n]` / `s[len]`。旧注释「AC set by entry stub」是已废的全局-STAC 假设,
P3 撤全局 stac 后必炸,这批提前拆掉。test 的 `dispatch_sys_execve` 改调 do_*(kernel path,
VFS 未挂载 → 负结果);`via_table` 保留 sys_execve(dispatch 机制,access_ok 拒 → -kEfault 也负 → PASS)。

### P0d(e9cff8e):signal 家族(整帧 copy + 翻译缝)

`sys_kill` / `sys_rt_sigaction` / `sys_rt_sigprocmask` 切两层:

- `do_kill_kernel` / `do_sigaction_kernel` / `do_sigprocmask_kernel`:纯 kernel-to-kernel
  (kernel `SigAction`/`SigSet`/Task)。`do_sigaction_kernel` 做 **old-before-new**(POSIX in-place
  安全);顺手把硬编码 `-22`/`-3` 换成 `kEinval`/`kEsrch`。
- `sys_*` 边界:**翻译缝**留在这里——user `UserSigAction` 的 `sa_handler`(0/1/addr)
  与 kernel `HandlerType` 互转、`sa_flags` bit ↔ bool 互转,都在 sys_* 做。
  `UserSigAction` **整帧 32B `copy_from/to_user`**(不逐字段拷);`SigSet` 走 `get/put_user`。

**根除的裸解引用**:`act`/`oact`/`set`/`oset`(旧「kernel maps caller AS」假设是全局 STAC,P3 撤后必炸)。
test_signal 的 sigaction/sigprocmask 改调 do_*(kernel SigAction/SigSet);sys_kill test 是标量
(无 user 指针),保留 sys_kill 边界透明。

### P0e(42d5640):杂项家族

`clock_gettime` / `pipe` / `getcwd` / `getdents` 各切两层,do_*_kernel 写 kernel buf(可能
block),sys_* 边界 `copy_to_user`:

- `do_clock_gettime_kernel(kernel ktimespec)`(`ktimespec` 提到 hpp public)+ copy_to_user。
- `do_pipe_kernel(FDTable, kernel int[2])`:build pipe graph + copy_to_user。
- `do_getcwd_kernel(kernel dst, size)`:`PathBuf` 暂存 + copy_to_user。
- `do_getdents_kernel(fd, kernel kname, count)`:readdir + copy_to_user。

`sys_ioctl` 这批**不改**——当前是返 `-ENOTTY` 的 stub,无 user 解引用。clock/getcwd/getdents
happy path 改 do_*;边界 case(NULL buf / bad fd / noncanonical addr)保留 sys_*。

## 验证

每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg
(单核 + `-smp2`)绿。P0a-P0e 稳定在 951/0(单核)+ 960/0(-smp2),stat/fstat/signal
等各家族 test 全 PASS。

> **关键认识(贯穿整个 SMAP 重做)**:run-kernel-test 的 test 都走 `do_*_kernel`(传 kernel
> 地址),**不碰 accessor**,所以它**不能**验证「真用户态路径走 accessor 读合法 user 内存」
> ——SMAP 在 test kernel 下根本不触发(假绿,F9 4 批同根)。accessor 的真闸是后面 P3 撤全局
> stac 后的 **ring-3 musl smoke**。这一层当时按「骨架照已验证模式、低风险」推进,P0g/P3
> 收口时才用 ring-3 兜底。

## 关键决策与教训

- **分层是正确性的载体,不是洁癖**。`do_*_kernel` 把「可以 block / 摧毁地址空间 / 持有内核
  状态」的逻辑和「碰用户内存」彻底切开,block-then-write 铁律才有落点。否则你没法保证
  「stac 窗口不跨 schedule」。这套分层后面 P0g、P3 直接复用,不用重设计。
- **accessor 窗口不阻塞,是整个 SMAP 模型的物理约束**(AC per-CPU + context_switch 不存
  RFLAGS),不是风格选择。违反它就是 F9 全局 stac 那个坑的重演。
- **M0 顺手修正 access_ok vs validate_user_ptr 的严格性缺陷**——后者放过内核高半地址,
  是潜在的安全绕过面。做基础设施时把这类「旧实现偏宽」的点一并收紧,比留到后面踩雷强。
- **没有 exception table 是临时妥协**。accessor 容错暂时靠 access_ok 预检 + PF handler
  demand-paging 兜底,做不到 Linux 那种「真 fault 截断返 -EFAULT」。这限制了负测试(没法
  写「绕 accessor 应 #PF」),是 memory 里 follow-up #2 的来源。

## 后续怎么接上的(本批是地基)

- **P0g**(8db56c4/3958fbc/179d776):path 家族(open/openat/creat/mkdir/chdir/unlink/rmdir)
  分层 do_*_kernel + `path_util` 的 `read_user_path` accessor(删 path 裸解引用)。`read_user_path`
  照 P0c `read_user_string` 骨架。详见 [P0g note](2026-06-28-smap-p0g-path-family.md)。
- **P3**(c186879):撤 syscall.S:61 + interrupts.S 3 处全局 stac(SMAP 真生效),三个剩余
  裸解引用(cleartid / futex_wait / signal_setup_frame)改局部 stac/clac。**本批(P0a-P0e)的
  家族分层就是 P3 能安全撤全局 stac 的前置**——没有 do_*/sys_* 切开 + accessor 化,撤全局
  stac 后这些路径全裸奔 #PF。
- **(B)/(P4)**:(B) stack canary 错配被 PF demand-page 掩盖的诊断、P4 清诊断,都建立在
  「PF handler 宽松 demand-page 仍在」的前提上,而这个前提正是本批暂用的容错契约。

## 残留(本批已知、非本批处理)

- **path 家族裸解引用**:P0a 末尾显式留下,由 P0g 收口(已做)。
- **test 残留 `X_addr` 未用变量**(P0b):非 -Werror 不阻塞,留后续统一清。
- **生产路径 accessor 盲区**:run-kernel-test 用 kernel 地址假绿,真用户态路径未被它验证
  (骨架照 P0c 低风险),P3 撤 stac 时 ring-3 musl smoke 兜底。
- **exception table 基建缺失**(M0 容错契约的根源):独立 follow-up(memory #2),需要建
  exception table 让 accessor 对真 fault 截断返 -EFAULT,并解锁负测试。
