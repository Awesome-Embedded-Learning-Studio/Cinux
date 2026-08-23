# 2026-08-14 LLM 全仓静态审计报告（七维度：workaround / 隐性 TODO / 过度抽象 / 并发 / 资源 / 错误处理 / 风格）

> **触发**：用户要求「做一轮静态扫描，专挖 LLM 写代码的三大坏习惯——workaround 绕根因、TODO 砖头忘掉、过度抽象」。
> **方法**：ultracode Workflow 扇出——11 子系统 + 4 跨切面共 15 个 find agent（七维度判定标准统一注入）→ 去重 → **每条 finding 派独立 skeptic 复核（默认判 not-real，Read 实际代码反驳，宁漏勿误）** → 合成。另 2 个补扫 agent 补限流缺口（drivers-input-net 子系统 + 过度抽象全局切面，**未经 skeptic 复核**）。共 186 agent / 7.18M tokens / 2607 工具调用。
> **覆盖**：kernel/ 全子系统 + libs/ext2（kernel/test/ 除外）。共 170 原始候选 → 62 confirmed（1 critical / 7 high / 22 medium / 14 low / 2 uncertain）+ 补扫 18 条。
> **登记**：发现按族归并为 DEBT-025 ~ DEBT-046 登记于 `../debt.md`；本报告为细节单一事实源。
>
> ## ⚠️ 登记时订正（重要度升级）
>
> **killpg UAF（正文 B 类 critical）被审计低估了**。正文写「一旦 task 内存被回收（文档化的终态）……」——实际 **DEBT-002 已于 2026-06-21（F-QA Q4e-3, `4bb6ca4`/`e6ce2f4`）修复**，task 现经 `exit_current → reap_deferred → delete` 真释放；同文件 signal.cpp:73 注释明说 "Tasks ARE freed now (DEBT-002 fixed)"。即 killpg:276 注释自称的安全前提（"tasks are never freed"）**已失效**，`unlock 后 signal_send(targets[i])` 的跨 CPU UAF 窗口**现在就存在**。与已修的 DEBT-022（ProcFS TOCTOU snapshot 修复）完全同族——那次修了 `/proc` 的锁外解引用，漏了这个姐妹点。**定性从「gated 在未来债上的潜在 UAF」升级为「活跃 UAF」**。
>
> 另：审计未覆盖 scheduler preempt 弧的 signal_check 内核态抢占路径（当时 B1 中间态），该缺口由战役批次 C 人工补查（见 debt.md DEBT-033 关联）。

---
# CinuxOS 代码审计报告 — drivers / net / syscall / proc / mm / fs 子系统

> **本次为静态扫描 + skeptic 复核,非运行时验证。** 所有 finding 基于代码阅读 + grep 证据链 + 与项目历史记忆交叉核对;结论需结合实际运行时(尤其 SMP 竞态类)再确认。已对若干高致命条目做了 spot-check 文件结构验证(AHCI 无 `lock_` 成员 vs NVMe 有 `Mutex lock_;`、sa_mask TODO 原文存在、task_deadline 死 hook 存在),均属实。

---

## 总结

本批审计在 drivers / net / syscall / proc / mm / fs 子系统中发现 **44 条 confirmed 缺陷**(已去重 sa_mask / getdents 等同源合并)。核心病灶高度集中:

1. **SMP 并发债是头号致命源**(D 类 14 条,其中 high 4 条)—— 与项目历史记忆一致。AHCI 共享 DMA 缓冲无锁、net 全栈(g_tcp/udp/arp + loopback ring)无锁、VMA 存储器 `vma_lock_` 声明了却只一处用、跨 CPU 信号 `sig_pending` 非原子 RMW、killpg 释放锁后用 Task* 等,都是"换了姿势迟早炸"的真实数据竞争。
2. **错误处理铁律违背普遍**(A/F 类合计 14 条)—— AHCI `init()` 返回 void 静默吞 4 条失败路径、framebuffer 丢弃 `map_2mb` 返回值、`new` 不检查 nullptr 直接 deref(make_node/AddressSpace/tmpfs/clone/dentry)、page_cache 把 read 结果 `static_cast<void>` 丢掉留全零页冒充文件内容、IoPhys OOM 时死循环静默掩盖。
3. **正确性缺口型 TODO**(B 类 14 条)—— sa_mask handler 期间不 block(critical/high)、interruptible sleep 未实现致 SIGKILL 无法打断 futex、getdents 小缓冲丢条目、rename 不替换/不调 nlink、socketpair/fork fd 表 OOM 泄漏。
4. **过度抽象**(C 类 7 条)—— IVMAStore 单实现纯虚接口、task_deadline / task_fork / max_frame / supports_zerocopy / UserPtr 等零消费者死代码。

| 维度 | 计数 | critical | high | medium | low |
|------|------|----------|------|--------|-----|
| **A** Workaround 绕根因 | 11 | 0 | 1 | 6 | 4 |
| **B** 隐性 TODO/砖头 | 11 | 1 | 1 | 5 | 4 |
| **C** 过度抽象 | 7 | 0 | 0 | 0 | 7 |
| **D** 并发/SMP | 14 | 0 | 4 | 6 | 4 |
| **E** 资源/契约 | 1 | 0 | 0 | 1 | 0 |
| **F** 错误处理 | 7 | 0 | 0 | 4 | 3 |
| **G** 风格/行数 | 1 | 0 | 0 | 0 | 1 |
| **合计** | **44(+2 uncertain 附录)** | **1** | **7** | **22** | **14** |

> 严重度统计中,sa_mask / getdents / socketpair 等同源条目按"最高一条"计入去重;附录 uncertain 2 条不计入主表。net 全栈无锁(net_init.cpp / loopback_device.cpp / tcp 表 / icmp 表)在 D 类按各自文件独立计数。

---

## A. Workaround 绕根因

### signal handler 期间 sa_mask 与本信号未 block,handler 可被同号递归重入  `kernel/proc/signal_frame.cpp:21`  [high]

- 维度/类别:A-workaround / signal-mask-not-applied(亦记 B 类,见 B 节同条)
- 详情:`signal_setup_frame()` 拿到 `act.sa_mask` 参数(`sys_signal.cpp:134` 传入)却标 `[[maybe_unused]]` 直接丢弃,第 22 行 TODO 明文承认"block sa_mask (+ sig) during the handler"未做。POSIX 规定进入 handler 时把当前信号 + sa_mask 中所有信号加入 `sig_blocked`,sigreturn 恢复。当前完全不修改 block mask,于是同一信号(或 sa_mask 中信号)在 handler 执行中再次到达会重入 `signal_setup_frame`,在用户栈上叠第二帧,破坏第一个 SignalFrame 保存的上下文 → sigreturn 恢复出垃圾寄存器/RIP。busybox/musl 普遍设 sa_mask,这是跨所有用自定义 handler 里程碑的正确性缺口。
- 建议:frame 建立时把旧 `sig_blocked` 存入 SignalFrame(加 `sig_blocked_saved` 字段),然后 `task->sig_blocked |= (sa_mask | sig_make_mask(sig))`(SA_NODEFER 时不加自身);sigreturn_handler 恢复 `task->sig_blocked = saved`。读写 `sig_blocked` 须配合 D 类 signal.cpp 的 sig_lock/atomic。

### sys_access 不 follow 符号链接,检查的是链接而非目标  `kernel/syscall/sys_access.cpp:73`  [medium]

- 维度/类别:A-workaround / missing-symlink-follow
- 详情:`do_access_kernel` 用 `fs->lookup(rel)` 解析路径,只遍历目录分量、不跟随尾随符号链接。POSIX access(2) 必须 follow。对 `/bin/sh`(指向 busybox 的符号链接)做 `access(X_OK)`,检查的是符号链接自身的 mode 而非 busybox 的权限位。`sys_stat`/`sys_open` 已正确改用 `vfs_lookup(Follow)`(sys_stat.cpp:69、sys_open.cpp:155),与历史 stat-follow 坑同族不一致。危险场景:ldso/启动脚本对 PATH 中符号链接 applet 做 `access(X_OK)`,拿到链接的错乱 mode,误判可执行性,导致符号链接程序启动失败。
- 建议:改用 `cinux::fs::vfs_lookup(resolved_path, LookupFlag::Follow, "/")`。access 无 NOFOLLOW 语义,一律 follow。

### sys_chmod 不 follow 符号链接,改的是链接而非目标  `kernel/syscall/sys_chmod.cpp:34`  [medium]

- 维度/类别:A-workaround / missing-symlink-follow
- 详情:`do_chmod_kernel` 用 `fs->lookup(rel_path)`,不跟随尾随符号链接。POSIX chmod(path) 必须跟随作用于目标;chmod 符号链接本身应返回 EOPNOTSUPP。当前对符号链接路径静默作用于链接 inode。对 `/usr/bin/cc`(指向 gcc-X.Y 的符号链接)chmod 会改链接而非 gcc 二进制,权限丢失/错位。
- 建议:改用 `vfs_lookup(resolved_path, LookupFlag::Follow, "/")`。如需 lchmod 另加 flag。

### IoPhys VMA 缺页 map 失败时静默 return,OOM 下陷入无噪 #PF 死循环  `kernel/arch/x86_64/page_fault.cpp:274`  [medium]

- 维度/类别:A-workaround / unresolved-fault-mask-spin
- 详情:设备 mmap(如 `/dev/fb0`)缺页时若 `g_vmm.map_nolock` 返回 false(中间页表页分配失败/OOM),handler 直接 `return`,不让出、不杀进程、不 panic。注释称"OOM resolves or the system panics",但该分支内根本没有 panic 调用 —— 持续 OOM 下它永远在 IoPhys 分支 return → 重执行同指令 → 同一 #PF → 再 return,形成无噪 CPU 死循环,看起来像挂死而非崩溃,debug 时连 backtrace 都抓不到。对比同文件 anonymous 路径(:390)会 fall through 到 SIGSEGV/panic,唯独 IoPhys 路径"假装解决"。典型 workaround:用 return 掩盖 PMM/VMM 失败,根因(OOM)还在。
- 建议:`map_nolock` 失败时:用户态访问 `signal_force_send(kSigbus)`(与 file-backed 读失败 :378 一致),内核态 `panic(frame)` 给明确"OOM-at-device-mmap"诊断。

### page_cache invalidate_range 把 read 结果丢弃,失败时留全零页冒充文件内容  `kernel/mm/page_cache.cpp:185`  [medium]

- 维度/类别:A-workaround / silent-error-swallow(亦 F 类)
- 详情:`invalidate_range` 是 write 后重读同步缓存:`memset(v, 0, ps)` 后 `static_cast<void>(inode->ops->read(inode, off, v, ps));` 把 read 的 `ErrorOr` 返回值整个丢弃。若重读失败(NVMe/AHCI I/O 错误、短读),页面停留在 memset 的全零状态 —— 既非旧内容也非新内容,调用方(`sys_read` / mmap demand paging)把这个全零页当文件真实内容返回。这正是历史 b3-ext2-agg-sigill 那类"coalesced I/O 读失败被静默掩盖 → 上层拿到垃圾/零 → cc1 崩"的同族问题。`get_page` 路径(:116-120)正确检查并 propagate read 错误,这里没对齐。
- 建议:检查 read 返回值,失败时标记该 CachedPage 为 invalid(强制下次 get_page 重填)或从 cache 移除并返回错误,绝不能留静默全零页。与 `get_page:118` 的 `if (!read_res.ok()) return read_res.error();` 对齐。

### mini handle_pf 打印后 IRETQ 返回,真缺页必然无限循环  `kernel/mini/arch/x86_64/exception_handlers.cpp:98`  [medium]

- 维度/类别:A-workaround / ignore-error-continue
- 详情:mini(早期引导)的 #PF handler 读完 CR2、打印错误码后直接 return,ISR stub 执行 IRETQ 回到引发缺页的同一条指令。任何真缺页(映射缺失/权限违例)都原样再触发,RIP 不变的死循环,串口被刷爆、CPU 永久占满。注释承认"Once VMM is added this will become the core handler",但把"未实现"当成了"安全继续"。big_kernel_loader 用 identity_map 主动建映射回避缺页,一旦映射漏了(staging buffer 与 PT_LOAD 重叠、1GB 大页路径被跳过)就直接死循环而不给诊断。
- 建议:缺页无法修复时 `cli; hlt` 停机并打印 CR2/RIP/error code,不要假装"继续"安全。与 FC29000 历史教训同源。

### mini ata dma_read 轮询超时被当成功返回  `kernel/mini/driver/ata.cpp:261`  [medium]

- 维度/类别:A-workaround / timeout-silent-success
- 详情:`for (i=0; i<50000000; i++)` 等 BM_STATUS_INTERRUPT,循环跑完仍未置位(驱动卡死/DMA 真失败)时无 `timed_out` 标志,代码继续走与成功相同的 Stop + 状态检查路径。超时场景下 ATA_STATUS_ERR|DF 很可能为 0,于是 `return true`,把没传完的缓冲当完整数据交给 ELF loader,加载的内核镜像可能损坏,跳转到大内核入口得到垃圾指令。
- 建议:循环退出后判断是否真看到 INTERRUPT,否则 kprintf 超时并 return false;循环外再读一次 bm_stat 二次确认 ACTIVE 清零。

### sys_socketpair 第二 fd 安装失败时显式泄漏第一个 fd  `kernel/syscall/sys_socketpair.cpp:64`  [low]

- 维度/类别:A-workaround / fd-leak-on-error(本批两条同族,此处合并见 B 节)
- 详情:`install_socket_fd(b)` 失败(EMFILE)时直接 return,fd0 留在 fd 表中,注释自承"For now leak the fd ... documented follow-up"。泄漏 fd 调用者无法回收(socketpair 已失败),File/Inode/Socket 永久驻留;长期 daemon 命中可累积耗尽 fd 槽位。同函数 copy_to_user(EFAULT)路径泄漏两个 fd。
- 建议:fd1 失败时 close fd0;EFAULT 路径 close fd0+fd1。复用 sys_socket 已有 close 路径。

### sys_utimensat 不 follow 符号链接 + 忽略 AT_SYMLINK_NOFOLLOW + times==NULL 用 0 占位  `kernel/syscall/sys_utimensat.cpp:56`  [low]

- 维度/类别:A-workaround / missing-symlink-follow + wall-clock-missing
- 详情:三重叠加:(1) `fs->lookup` 不 follow 尾随符号链接;(2) flags 被 `[[maybe_unused]]` 丢弃,AT_SYMLINK_NOFOLLOW/AT_EMPTY_PATH 全忽略(注释 line 59 承认);(3) times==NULL 的 'now' 情况用 0 占位(注释 "placeholder follow-up")。危险场景:`touch /bin/sh` 把链接 mtime 设成 epoch 0;`touch file` 把 atime/mtime 设成 0 而非当前时间,make/构建系统依赖 mtime 判断文件新旧会全量重建或永不重建。
- 建议:(1) 改 vfs_lookup(Follow),按 flags 切 NoFollow;(2) times==NULL 用 `sys_clock_gettime` 同款 boot_epoch_ns()+mono 填当前时间(基础设施已存在,见 sys_clock_gettime.cpp:55-57)。

### queue_signal/signal_pick_deliverable 对 sig_pending 非原子 RMW 跨 CPU 丢信号  `kernel/proc/signal.cpp:213`  [low→high,见 D 节合并] 

- 维度/类别:A-workaround + D-concurrency(本条在 D 节 high 详述)
- 详情:`sig_set_add`(`set |= mask`,非原子 RMW)。生产者(queue_signal 在 sender CPU)与消费者(signal_pick_deliverable 在 target CPU)并发读改写同一 SigSet 字,丢位 → 信号丢失/幽灵信号。属"看起来不会并发实则会"。

### killpg 静默截断 64 个目标,超出的进程收不到信号  `kernel/proc/signal.cpp:277`  [low]

- 维度/类别:A-workaround / silent-truncate
- 详情:killpg 把匹配 pgid 的 task 快照进 `Task* targets[64]`,循环里 `ntargets < kMaxKillTargets` 截断;`itimer_real_tick` 的 `expired[64]` 同样。超过 64 个同组进程时第 65 个起收不到信号,函数仍返回 sent(>0),调用者以为成功。典型"固定上限 + 静默截断"掩盖根因。场景:make -j64+ job server 发 SIGTERM 部分子进程漏收 → 不退出 → 父 hang。
- 建议:动态分配或两遍扫描,超限至少 kprintf WARN 并返回部分成功计数,别静默吞。

### chown 不 follow 符号链接  `kernel/syscall/sys_chown.cpp:33`  [low]

- 维度/类别:A-workaround / missing-symlink-follow
- 详情:`do_chown_kernel` 用 `fs->lookup` 不 follow。POSIX chown(path) 默认 follow 作用于目标。install/chown 对符号链接设属主改的是链接 inode,属主变更丢失。
- 建议:改 vfs_lookup(Follow)。fchownat 有 AT_SYMLINK_NOFOLLOW,但 sys_chown 无 flag 一律 follow。

### pipe release_*_ref 下溢保护静默 return 掩盖不平衡 ref bug  `kernel/ipc/pipe.cpp:319`  [low]

- 维度/类别:A-workaround / silent-underflow-guard
- 详情:`release_read_ref`/`release_write_ref` 在 `read_refs_ == 0` 时 `return`(`// underflow guard`)。一旦 add/release 不平衡(DEBT-023 这类 ref 最易出错),这里静默吞,既不 kprintf 也不 panic,真实泄漏/双释被掩盖,pipe 状态(reader_open_/wake_all)行为错乱却无日志可追。
- 建议:下溢时至少 kprintf WARN(DEBT-023 关联,打印 read_refs_ 与 caller),开发构建直接 panic。

### waitpid 自旋超时后"leak task + 保留 PID"返回 Ok  `kernel/proc/process_new.cpp:339`  [low]

- 维度/类别:A-workaround / leak-as-fallback
- 详情:50M 自旋后若 on_cpu 仍 != -1,代码 kprintf WARN 后 `return WaitpidResult::Ok` —— 不 free stack、不 delete task、不 free pid。返回 Ok 是危险的谎言(父进程以为 reap 成功,下次 waitpid 同 pid 返回 NoChildren 但 PID 被永久占住 → PID 耗尽;Task/栈永久泄漏 → 长期 init 反复 fork/reap 累积内存)。注释自承"a pinned child is debuggable; a use-after-free is not" —— 用泄漏规避 UAF,根因(为什么 on_cpu 迟迟不回 -1)没查。
- 建议:超时应 kpanic(scheduler on_cpu 同步真坏了)或把泄漏 task 挂 orphan reap 链表由后台 kthread 重试 free 并 free PID。返回 Ok 是危险的谎言。

### clear_user_mappings 清 PTE 不 TLB flush  `kernel/proc/execve.cpp:68`  [low]

- 维度/类别:A-workaround / no-tlb-flush
- 详情:`clear_user_mappings` 把 256 个 PML4 条目全置 0 释放页表,全程不调 flush_tlb。对单线程进程靠下一次 context_switch 切 cr3 隐式 flush 勉强 OK,但:(1) execve 后立即返回用户态(同一 cr3 无切换),残留 TLB 让已释放页短期内仍可访问;(2) CLONE_VM 线程组里别的线程共享 cr3,TLB 缓存着被 clear 的映射,继续读写已 free 物理页 → corruption。
- 建议:`clear_user_mappings` 结束时 `flush_tlb_all()`(或至少 invlpg 关键页)。当前依赖 cr3 切换的隐式 flush 是脆弱时序假设。

### capture_first_gp 用 static bool 只抓"第一个"fault,真正致命 fault 的 debug 信息永远丢  `kernel/arch/x86_64/fault_diag.cpp:149`  [low]

- 维度/类别:A-workaround / first-fault-only-capture
- 详情:`capture_first_gp` 在 handle_gp 入口无条件调用,`static bool dumped` 保证一次性。开机后第一个 #GP(哪怕是被 SIGILL 正常恢复的 user-mode #GP)就把 dumped 锁死,之后真正导致 panic 的 #GP 不再被 capture,debug.log 里只有那个无关首帧,定位根因时被误导。
- 建议:#GP 的 capture 只在 from_user==false(即将 panic)或 task==nullptr 时触发,跳过可恢复 user-mode SIGILL 路径;或改为"capture until first PANIC"。

---

## B. 隐性 TODO / 砖头

### killpg 释放锁后用 Task* 发送信号,依赖未修的 DEBT-002(UAF 前提)  `kernel/proc/signal.cpp:276`  [critical]

- 维度/类别:B-todo-debt / killpg-task-lifetime-UAF
- 详情:killpg 在 `g_registry_lock` 下把最多 64 个 Task* 快照进栈数组,释放锁后再循环 `signal_send(targets[i])`。line 276 注释自承只在"tasks are never freed (DEBT-002 not yet fixed); Q4e must revisit"下安全。DEBT-002 是 deferred-free list(process.hpp:281)。一旦 task 内存被回收(文档化的终态),target 可在 unlock 与 signal_send 之间 exit + 被 free → dereference 已释放/被 slab 复用的内存 → UAF,或信号被发给一个全新的无关 task。这是 gated 在未修 DEBT-002 上的潜在 UAF。
- 建议:signal_send 循环期间持 registry lock(queue_signal 不睡眠,持锁安全),或按 target 取 per-task refcount(acquire on lookup, release after send,类比 Linux tasklist_lock + get_task_struct)。把 DEBT-002 登记为此代码的硬阻断项。

### sa_mask handler 期间不 block(literal TODO,见 A 节 high 同条)  `kernel/proc/signal_frame.cpp:21`  [high]

- 维度/类别:B-todo-debt / sa-mask-not-blocked
- 详情:与 A 节 high 同一条,跨 sys_signal.cpp:57(正确存 sa_mask)与 signal_frame.cpp:21(`[[maybe_unused]]` 丢弃 + TODO)两文件确认。属跨里程碑未处理的正确性缺口。
- 建议:见 A 节。

### interruptible sleep 未实现,SIGKILL 无法打断 futex/waitpid/Mutex/Semaphore 阻塞  `kernel/proc/process.hpp:291`  [medium]

- 维度/类别:B-todo-debt / interruptible-sleep-unimplemented
- 详情:多处注释(signal.cpp:217、process.hpp:291、scheduler_block.cpp:119)承认只有 `wait_queue_head != nullptr` 的用户 IO 等待可被信号唤醒 EINTR,而 futex(FUTEX_WAIT)/waitpid/Mutex/Semaphore 的阻塞(`wait_queue_head==nullptr`)是纯不可中断的。后果:线程在 FUTEX_WAIT 上阻塞时 SIGKILL 无法在阻塞期间生效(只能等自然醒来后派发);SIGSTOP/SIGTERM 对 futex-waiter 无响应。注释把这推给"broad interruptible sleep TODO",跨多个里程碑未落地,是正确性缺口。
- 建议:要么把 interruptible sleep 登记到 document/todo/quality/debt.md 给时间线;要么给 Mutex/Semaphore/futex/waitpid 的 prepare_to_wait 路径也接 signal_deliverable_pending 检查(像 schedule_blocked 的 IO 分支那样),让 SIGKILL/SIGTERM 能打断。

### getdents64 缓冲区满时静默丢失已推进 offset 的目录条目  `kernel/syscall/sys_getdents64.cpp:64`  [medium]

- 维度/类别:B-todo-debt / getdents-entry-loss
- 详情:循环先调 `do_getdents_kernel` 读一个条目(它内部 `file->offset++`),再判断 `off+reclen > count` 超限则 break —— 刚读出的条目因 offset 已越过而永久丢失,下次 getdents64 从下一个条目开始。注释 line 66-68 承认"the entry is lost ... revisit (lseek-back / peek-ahead) if a caller does",无 DEBT tag、无里程碑、无小缓冲测试。Linux 保证不丢(要么放进去要么不推进)。危险场景:小缓冲遍历目录(musl/glibc 默认大缓冲,但应用层或 test 可能用小块)偶发丢一个目录项,ls/find/cp -r 漏文件且不可恢复。
- 建议:`do_getdents_kernel` 读条目前先 peek 不推进,或本层判断放得下后再推进 offset;最简把"读取"与"offset 推进"解耦(加 peek 接口)。Linux 模型:readdir 读到但缓冲放不下时不消费该条目。

### rename 拒绝覆盖已存在目标且目录 rename 不调 nlink  `libs/ext2/ext2_links.cpp:198`  [medium]

- 维度/类别:B-todo-debt / rename-correctness-gap
- 详情:line 220-224 "refuse to overwrite an existing dst entry. (Linux rename(2) replaces it; follow-up.)" —— `mv a b`(b 已存在)会失败,而 POSIX 要求原子替换,常见构建脚本/make 依赖此语义。line 258-260 "NOTE (follow-up): renaming a directory should adjust the parent link counts ... known simplification" —— 移动目录后目标父目录 nlink 不 +1、源父目录不 -1,后续 stat 报错 nlink、`find` 行为异常,严重时 rmdir 因 nlink 永远非零无法清理。两条都是"正确性缺口 + 无时间线 follow-up"。
- 建议:(1) rename 目标存在时先 unlink 目标再 add;(2) 目录 rename 时 src 父 `i_links_count--`、dst 父 `i_links_count++` 并 write_disk_inode。至少在 ROADMAP/quality/debt.md 登记并给里程碑。

### sys_signal: sa_mask 存了但 signal_setup_frame 丢弃(跨文件确认)  `kernel/syscall/sys_signal.cpp:46`  [medium]

- 维度/类别:B-todo-debt / sa_mask-not-blocked(与 A 节 high / signal_frame.cpp 同条,入口在此)
- 详情:跨文件确认 sys_signal.cpp:57 正确把用户 sa_mask 拷进内核 SigAction,但实际投递时(signal_frame.cpp:134 传 act.sa_mask)被丢弃,grep 确认 kernel/proc/signal.cpp 无 `sig_blocked = (sa_mask|sig)` 赋值。
- 建议:见 A 节 high。

### AF_UNIX stream send() 阻塞 socket 在 peer RX 满时返回 EAGAIN,无 flow control  `kernel/net/unix_socket.cpp:227`  [medium]

- 维度/类别:B-todo-debt / stream-send-no-flow-control
- 详情:POSIX 阻塞 send() 在 stream socket 上必须在缓冲有空时阻塞至可写(modulo signals)。此处 peer 4KB ring 满时立即 return WouldBlock(EAGAIN)。注释承认"True send-side flow control ... is a follow-up; for now report EAGAIN so a looping caller backpressures itself"。正确阻塞调用者会死循环烧满一核(没有 wake),naive 调用者则拿到 blocking socket 上的虚假 EAGAIN。recv()/accept() 已正确用 wait queue,只 send() 缺。
- 建议:镜像 recv() 模式:peer ring 满时 prepare_to_wait 到 per-socket send_waiters_ 队列,recv() pop_batch 腾出空间后 wake_one(send_waiters_),并 honor EINTR。给 UnixSocket 加 send_waiters_。

### sys_open O_CLOEXEC 未接 FDTable  `kernel/syscall/sys_open.cpp:244`  [low]

- 维度/类别:B-todo-debt / cloexec-not-wired
- 详情:接受 O_CLOEXEC flag(line 112-113 kOCloexec),但 alloc 后 line 247 注释"close-on-exec not yet wired into FDTable"直接 return fd。F_SETFD 倒能设 cloexec(`f->cloexec`)。危险场景:O_CLOEXEC 打开的 fd 在 execve 时不被关闭,泄漏进子进程(经典 security/correctness bug,daemon 防泄漏模式)。
- 建议:alloc 成功后 `if (flags & kOCloexec) { if (File* f = current_fd_table().get(fd)) f->cloexec = true; }`,复用 F_SETFD 已有 cloexec 字段。

### pipe close 不释放 inode/ops/Pipe  `kernel/syscall/sys_pipe.cpp:32`  [low]

- 维度/类别:B-todo-debt / pipe-inode-not-freed
- 详情:注释"closing a pipe fd frees the File but not the inode/ops/Pipe -- a known hobby-OS limitation"。Pipe+2 Inode+2 Ops 在 fd 关闭后仍驻留。shell 管道密集场景(每条 `|` 一次 pipe)长期运行内存单调增长 → OOM。属 InodeOps 缺 release hook 的系统级债(socket 路径 sys_socket.cpp:156-158 同注释)。
- 建议:给 InodeOps 加 release/close hook(File 析构时调 ops->release),pipe/socket close 时 delete 对应结构。统一加 release hook 一次性解决。

### SOCK_NONBLOCK 在 accept4 静默丢弃  `kernel/syscall/sys_socket.cpp:196`  [low]

- 维度/类别:B-todo-debt / sock-nonblock-ignored
- 详情:只处理 SOCK_CLOEXEC,SOCK_NONBLOCK 被 flags 吞掉,accept 出来的 fd 阻塞,而调用者明确要非阻塞。危险场景:网络服务器 accept4(SOCK_NONBLOCK) 期望新连接非阻塞,实际阻塞 → recv 阻塞整个事件循环 → 服务卡死。flag 被静默忽略比返回错误更危险。
- 建议:加 File/O_NONBLOCK 标志,accept4 与 socket 解析 SOCK_NONBLOCK 并设置;至少未实现时对带 SOCK_NONBLOCK 的 accept4 返回 -EINVAL 而非静默给阻塞 fd。

### SA_RESETHAND / SA_RESTART 存了但从不应用  `kernel/proc/signal_frame.cpp:133`  [low]

- 维度/类别:B-todo-debt / sa-resethand-not-enforced
- 详情:sys_signal.cpp:55-56 存 sa_restart/sa_resethand,但 signal.cpp / signal_frame.cpp 无任何消费方。SA_RESETHAND:one-shot SIGSEGV handler 第二次 fault 不回 default 而是循环在自己 handler 里,掩盖真实 crash。SA_RESTART:被中断 syscall 即便 app 请求 auto-restart 仍返回 EINTR。
- 建议:custom delivery 分支若 act.sa_resethand,投递后把 disposition 翻回 kDefault;SA_RESTART 在 pipe/socket/poll 的 -EINTR 路径若 handler 有 sa_restart 则自动重试。

### mini idt #BP 门注释与 DPL 自相矛盾  `kernel/mini/arch/x86_64/idt.cpp:84`  [low]

- 维度/类别:B-todo-debt / ring0-for-now(stale misleading comment)
- 详情:注释写"DPL=3 allows INT3 from user mode (though we only have ring 0 for now)",但 `0x8F` 的 DPL 位(bits 6-5)= 00,即 DPL=0。读者误以为用户态能 INT3 或以为是临时放宽。
- 建议:删除"DPL=3 allows INT3 from user mode"误导行,保留"DPL=00, ring0 only"。

### draw_bitmap 的 0x00000000 colorkey 透明 hack(已被取代,全仓无调用者)  `kernel/drivers/canvas.hpp:177`  [low]

- 维度/类别:B-todo-debt / dead-workaround(stale colorkey hack)
- 详情:draw_bitmap 把 0x00000000 当全透明跳过(canvas.cpp:311),canvas.hpp:187-188 注释自承"legacy draw_bitmap() colorkey hack"且使纯黑不透明像素无法绘制。替换实现 draw_bitmap_masked(用 1-bpp mask)才是 F13 §4d 图标路径。grep 全仓 draw_bitmap 在 canvas 之外仅一句注释引用,无真实调用者。典型"绕根因 workaround 留在公共 API 里"。
- 建议:删除 draw_bitmap 与实现,或并入 draw_bitmap_masked(mask=null 即全不透明),消除误导性 colorkey API。

### B3a agg 段注释自称"agg OFF"但实际 agg>1 激活,且 buffer 名写错  `libs/ext2/ext2_common.cpp:130`  [low]

- 维度/类别:B-todo-debt / stale-misleading-comment
- 详情:line 130 "B3a agg OFF (follow-up): agg>1 read_disk_range SIGILLs ... Keep agg=1",但 line 140-149 的 while 循环主动把 agg 推到 4,line 151 `if (agg > 1)` 分支真实执行 read_disk_range。line 152/163 说 DMA 落 `block_buf_`,但 line 164 实际是 `read_disk_range(disk_block, agg, scratch.get())` —— 落 scratch。近 30 行注释全在描述"曾经 SIGILL、现已用 bounded memcpy 修好"的废弃状态,反而掩盖了已修复的正确性。
- 建议:删除/重写:agg>1 已启用,DMA 落 scratch(SMP 安全),bounded memcpy 防尾部越界(即当年 SIGILL 的修复)。保留"为什么不直接 DMA 进 dst"这一条价值。

### read() agg>1 注释把 buffer 名写成 block_buf_(实为 scratch)  `libs/ext2/ext2_common.cpp:152`  [low]

- 维度/类别:B-todo-debt / stale-misleading-comment(与上一条同注释群)
- 详情:line 152 "into block_buf_ (scratch)" 与 line 163 "Land the DMA in block_buf_" 把 DMA 目标写成 block_buf_,line 164 实参是 `scratch.get()`。调试时据注释查 block_buf_ 状态会完全扑空。
- 建议:把两处 block_buf_ 改为 scratch,与 line 164 实参一致。与上一条合并修复。

---

## C. 过度抽象

### IVMAStore 单一实现的纯虚接口  `kernel/mm/vma.hpp:133`  [low]

- 维度/类别:C-over-abstraction / single-impl-pure-virtual
- 详情:IVMAStore(9 个纯虚:insert/remove/find/find_free_area/first/next/count/clear/~)只有一个子类 LinkedListVMAStore,注释("future O(log n) tree backend")自承是为想象的红黑树留。每次 page-fault / mmap 都为这个永不发生的 swap 付 vtable 间接。AddressSpace 直接存具体类型 `LinkedListVMAStore vma_store_;`(address_space.hpp:170),连字段都知道只有一个。
- 建议:把 IVMAStore 折叠进 LinkedListVMAStore(方法改 non-virtual)。真要写 tree backend 时再抽接口,机械可逆。

### SchedulingClass::task_deadline 零调用零 override 的死 hook  `kernel/proc/scheduler.hpp:68`  [low]

- 维度/类别:C-over-abstraction / yagni-task-deadline-hook
- 详情:`virtual uint64_t task_deadline(Task*)`,roundrobin.cpp:32 给恒返回 0 的默认实现,注释"Reserved for future RT scheduling classes"。grep 全仓零非测试 caller,RoundRobin 不 override,唯一调用是 test_scheduler.cpp 断言它返回 0。纯虚接口里的 dead method,每次 vtable 带着它。scheduler.hpp 自己注释都写"Reserved for future"。
- 建议:删 task_deadline(声明+默认实现+注释)。真有 RT/deadline 调度类时再加,pick_next_from 多类遍历若长期只有一类也可简化为直接调 RoundRobin::pick_next()。

### SchedulingClass::task_fork 在真 fork()/clone() 里从不调用  `kernel/proc/scheduler.hpp:64`  [low]

- 维度/类别:C-over-abstraction / yagni-task-fork-unused
- 详情:fork.cpp/clone.cpp 对子进程 priority/quantum 处理是直接 memcpy + pick_next/build 重填(process.hpp:136 注释"Refilled by pick_next / task_fork / build"),从不调 `child->sched_class->task_fork()`。grep 确认唯一调用点是 test_scheduler.cpp::test_task_fork_inherits_priority。RoundRobin::task_fork 定义存在但无人调。要么是"为想象未来留的 hook",要么是 fork 本该调用却忘了 —— 无论哪种都是债。
- 建议:要么在 fork()/clone() 真调一次(替换 memcpy+pick_next 重填,让策略自洽),要么删钩子。当前是"有接口、有实现、没人调"。

### LookupFlag::Create / Excl 全仓无调用  `kernel/fs/vfs_lookup.hpp:36`  [low]

- 维度/类别:C-over-abstraction / dead-enum-values-yagni
- 详情:定义 Create/Excl 并注释"Advisory: O_CREAT/O_EXCL semantics handled by the caller"。grep 全仓 `LookupFlag::Create|Excl` 在 kernel/ 下仅定义处。sys_open 实际用 Parent flag 拿 leaf 后自查存在性,Create/Excl 从不传入。典型"预留但无人用的字段"。增加枚举语义噪声:读者误以为 vfs_lookup 处理 O_CREAT/EXCL。
- 建议:删 Create/Excl。未来真要内部处理再加。

### NetDevice::max_frame / supports_zerocopy / link_up 零决策调用  `kernel/net/net_device.hpp:51`  [low]

- 维度/类别:C-over-abstraction / yagni-capability-hooks-unused
- 详情:跨文件核实,grep `max_frame()/.max_frame()` 除 override 声明和 doc 外零决策调用(net_device.hpp 注释自承 loopback 65536,但 loopback_device.hpp 实际 kBufSize=1518,doc 还和实现不符)。`supports_zerocopy()` 全仓只有 override+注释,无 `if(dev.supports_zerocopy())`。`link_up()` 唯一非 override 调用是 test_e1000.cpp 的 printf 诊断。三个是为"未来 virtio"预留的能力探测接口,dispatch/RX 走同一条 copy 路径完全不读它们。NetDevice 抽象本身合理(3+ 实现),但这三个方法是死代码。
- 建议:移除 max_frame/supports_zerocopy/link_up;真有 zero-copy virtio 或 MTU 协商时再加。保留 mac()/has_ethernet_header()(dispatch 真在用)。

### UserPtr<T> 零生产消费者(DEBT-019 脚手架)  `kernel/lib/user_ptr.hpp:45`  [low]

- 维度/类别:C-over-abstraction / yagni-zero-consumer-scaffold
- 详情:grep 全仓 UserPtr 引用除自测 test_user_ptr.cpp 外无任何 syscall/内核路径用它。头注释自承"HONEST LIMITATION: introducing UserPtr does NOT make user pointers safer — it has no consumer yet. It is type-first scaffolding for the DEBT-019 fix."。operator T()/->/*() 全直通裸指针零语义。它引入假的"安全"信号(看到 UserPtr 以为用户指针被处理了),实际 validate 还得调用方手动调。
- 建议:删掉直到 DEBT-019 真正落地 access_ok/copy_to_user 时再引入;或在那批把 syscall 边界真改用 UserPtr。当前留着是纯债。

### EventQueue + Mouse::poll 是 GUI 用户态化后遗留死代码,keyboard listener 仍往死队列 enqueue  `kernel/gui/event.hpp:110`  [low]

- 维度/类别:C-over-abstraction / dead-abstraction-no-consumer(亦 D 类,见 D 节)
- 详情:gui 用户态化(b3b)后内核 gui_worker 线程被删,EventQueue.dequeue 的唯一消费者 Mouse::poll(mouse.cpp:391)无 caller。但生产端还活:gui_init.cpp:38 on_key_event 仍 `Mouse::event_queue().enqueue(gui_ev)` 把每个键盘事件塞进无人读队列 —— 纯浪费(每键一次拷贝+环形写)。event.cpp 整个 TU 是死代码。
- 建议:确认 b3b 后内核侧不需内核内 EventQueue,删除 event.hpp/cpp、Mouse::poll、Mouse::event_queue(),并去掉 gui_init.cpp:38 的 enqueue(键盘已通过 InputEventDevice::push_event 进 /dev/event0 给用户态)。

---

## D. 并发 / SMP

### AHCI 共享 DMA 缓冲 dma_buf_ 全程无锁  `kernel/drivers/ahci/ahci_block_device.hpp:74`  [high]

- 维度/类别:D-concurrency / SMP-shared-DMA-buffer-unlocked
- 详情:`read_blocks`/`write_blocks` 都改 dma_buf_(memcpy 进、把其 phys 交给 AHCI、memcpy 出)无任何互斥。NVMe 的 hpp 明文记录为何要锁("production -smp 2 + gcc fork chain once exposed DmaBuffer corruption -> garbage ext2 block numbers"),virtio_blk.hpp 带同样 Spinlock,AHCI 是这堂课落地时被漏掉的。生产 -smp 下两 CPU 并发块读(如 page-fault 路径 + 另一 fs op)重叠缓冲:CPU A 的 memcpy 覆盖 CPU B 正在 DMA 的数据,或一 CPU 读时另一 CPU 的 AHCI 命令仍 DMA 进同一 phys。结果是静默 ext2 块损坏 —— 正是 749e7db 为 NVMe 修的那类 bug。init.cpp 的 boot-disk 兜底在某些配置仍走 AHCI,所以这是活的。spot-check 已确认:ahci_block_device.hpp 无 `lock_` 成员,而 nvme_block_device.hpp:81 有 `Mutex lock_;`。
- 建议:加 `cinux::proc::Mutex lock_;` 成员,把 read_blocks/write_blocks/flush 体包进 `auto g = lock_.guard();`,照 nvme_block_device.cpp:47 做。用 Mutex(非 Spinlock)以便 AHCI poll 将来能 block。

### VMA 存储 vma_lock_ 声明了却只 signal_frame 一处用,page-fault + mmap/munmap/mprotect 跨 CPU 无锁访问  `kernel/mm/address_space.hpp:147`  [high]

- 维度/类别:D-concurrency / VMA-store-shared-no-lock-SMP
- 详情:头注释(vma.hpp:31)说"The owning AddressSpace serialises access with its own lock (batch 2)",vma_lock_ 正为此提供。实际唯一 acquirer 是 signal_frame.cpp:44。page-fault handler(page_fault.cpp:187)在 CPU0 的 IF=0 下读 `vmas().find(fault_addr)`,而 sys_mmap(:156/167/184/189)、sys_munmap(:233)、sys_mprotect(:265/292/293/301)在 CPU1 改/遍历同一 intrusive 链表且无锁。LinkedListVMAStore insert/remove splice 节点并 free 它们(`delete cur`),缺页 CPU 同时走 head_->next。典型 SMP UAF/torn-list 竞态 —— 正是 CinuxOS 历史最大 bug 源(scheduler/IPC races 那一类)。IF=0 只屏蔽本 CPU 中断,不屏蔽另一 CPU 的 syscall。
- 建议:page_fault.cpp / sys_mmap / sys_munmap / sys_mprotect / fork / execve 每处 `vmas()` 访问都取 `vma_lock_`(fault 路径用 irq_guard —— IF=0 下 idempotent;syscall 路径用普通 guard)。基础设施已存在,只是没用。

### net_poll kthread 与 app 线程并发访问 g_tcp/udp/icmp/arp/ipv4/stack/lo 内部表,全栈无锁  `kernel/drivers/net/net_init.cpp:71`  [high]

- 维度/类别:D-concurrency / smp-unlocked-shared-module-state
- 详情:`net_poll_entry()` 是独立 kthread,调度到任意 CPU 循环 `g_stack.poll()`。poll() → Ipv4Module::on_frame → handle() 读/写 TcpModule::cons_[]/listens_[]、UdpModule::ports_[]、IcmpModule::raw_sockets_[];而 app 线程在 sys_socket/bind/connect/send/recv/close 路径调 tcp_.listen/connect/send/close/set_listener、udp_.bind/unbind、icmp_.register/unregister_raw_socket 改同一批表。这些表与 NetStack::devs_[]/protos_[] 全是裸数组,net/ 下 grep 零 Spinlock/guard。CinuxOS 默认 -smp 2,且 MEMORY 记载 SMP race 是历史最大 bug 源。具体炸点:app connect() alloc cons_[] 槽位的同时 poll 线程 handle() 扫同数组;app close() 把 `ports_[i].l=nullptr` 的窗口期,poll on_udp() 刚好 deref 那个 UdpListener* → UAF 或重复分发。
- 建议:给 TcpModule/UdpModule/IcmpModule/NetStack 各加一把 Spinlock,handle()/poll() 持锁遍历,listen/bind/connect/stop_listen/unbind/register|unregister_raw_socket 持锁改表;注意 send_segment 内回发(poll 持锁时不能持锁睡眠/做 DMA,把 TX 移出锁段)。短期最低限度:net_poll_entry 与所有 socket syscall 路径用同一把 net 大锁串行化。

### LoopbackDevice 的 storage_/state_/fifo_/head_/tail_ 无锁,send_l3 与 poll_rx+recycle 并发踩  `kernel/net/loopback_device.cpp:21`  [high]

- 维度/类别:D-concurrency / smp-unlocked-ring-buffer
- 详情:send_l3 扫 state_[] 找 Free 槽、memcpy 进 storage_、push fifo_、推 tail_;poll_rx 弹 head_、置 state_ InFlight、把 storage_ 指针交 dispatcher;recycle 用指针算 slot 索引置 Free。三函数操作同一组共享数组,但 loopback_device.{hpp,cpp} grep lock|Spinlock|irq 零命中。Loopback 是 AF_INET 127.0.0.1 流量主路径,app 在一 CPU tcp_.send→ipv4.send→stack.send_l3→lo.send_l3,net_poll kthread 在另一 CPU poll()→lo.poll_rx()+lo.recycle()。竞争表现:两 CPU 同时扫到同一 Free 槽 → 同一 fifo 位被两个 len_ 覆盖 → 数据错乱;send 推 tail_ 与 poll 推 head_ 非原子 → fifo_/head_/tail_ 不一致 → 丢帧或 recycle 拿到错 slot(指针反推索引,base 假设可能被越界打破)。
- 建议:给 LoopbackDevice 加一把 Spinlock 保护 state_/fifo_/head_/tail_(send_l3/poll_rx/recycle 全持锁,memcpy 在锁内,kSlots=8 极短可接受)。

### queue_signal 对 target->sig_pending/state/sig_forced 无锁 RMW,与目标 CPU 投递路径并发  `kernel/proc/signal.cpp:204`  [medium]

- 维度/类别:D-concurrency / shared-state-unlocked-smp / unsynchronized-sig-pending-RMW
- 详情:queue_signal 在 sender CPU 做 `target->sig_pending &= ~stop_mask`(:204, RMW)、`target->state = Ready`(:208)、`sig_set_add(target->sig_pending, sig)`(:213)、`sig_set_add(target->sig_forced, sig)`(:189)、`unblock(target)`(:220,unblock 内 task->state 也是无锁 RMW)。这些字段被 target CPU 无锁读:signal_deliverable_pending()(:257)、signal_pick_deliverable、sig_forced consume。-smp 2 下 CPU A kill/queue_signal 写 sig_pending 的同时 CPU B 目标任务正读它做投递判定,`&=`/`sig_set_add` 的 RMW 丢位 → 信号丢失(POSIX 不可接受)或幽灵信号;task->state 竞态让 unblock 与目标任务自身状态翻转互相覆盖(重复入队/唤醒丢失)。queue_signal 是跨 CPU 信号投递核心路径(gcc/shell/init 都用)。sig_forced 注释自称"SMP-safe",但 sig_set_add 仍非原子 RMW。
- 建议:给 Task 加 sig_lock(Spinlock),queue_signal 与 signal_pick_deliverable/deliverable_pending/consume 在该锁下读写;或对 sig_pending/sig_forced 用 atomic 位操作(atomic OR/AND)消除 RMW 竞态。类比 Linux per-task sighand->siglock。

### poll_events 同时被 hard-IRQ 与 thread context 调用且无锁  `kernel/drivers/usb/xhci_controller.cpp:272`  [medium]

- 维度/类别:D-concurrency / IRQ-vs-thread-shared-state-no-lock
- 详情:poll_events() 读/写 last_cmd_completion_、last_transfer_event_、event_ring_.dequeue()、遍历 by_slot_[] 调 listener。同函数从两条路径达:(1) MSI-X ISR 经 xhci_irq_handler→event_irq_thunk→poll_events;(2) thread context 经 usb_init.cpp:166 poll_input()(gui_worker 每帧调)和 run_command/run_transfer 内部 poll_events 循环。MSI-X 事件在 gui_worker 正 poll_events 中途 fire 时,ISR 与 thread 并发推进同一 EventRing dequeue 游标(dequeue_++ 可能丢失/双 apply)、互相覆盖 last_transfer_event_、把同一 Transfer Event dispatch 两次(双注一个 mouse report)或丢弃。头注释"no lock needed"只对 register_transfer_listener 注册成立,不对 poll_events 本身。真 HW + 正常 MSI-X 下 routinely fire。
- 建议:要么 (a) thread-context poll 路径用 Spinlock::irq_guard 关中断使 {ISR, task} 同时只一个跑 dequeue 循环;要么 (b) 把所有 listener dispatch + last_*_event_ 捕获移出 ISR 进 threaded poll_input(),ISR 只置"event pending"标志 + EOI(永不碰 ring)。(b) 匹配生产本就从 gui_worker poll 的现实。

### execute_command 硬编码 slot 0 且 AHCI 无端口锁,并发改命令表  `kernel/drivers/ahci/ahci.cpp:217`  [medium]

- 维度/类别:D-concurrency / single-cmd-slot-no-port-lock
- 详情:每个 read/write/identify/flush 都 `execute_command(port_index, 0, ...)` 复用 slot 0 和单一 command-table 区(cmd_list_phys + CMD_SLOTS*sizeof(header))。即使 AHCIBlockDevice 加了 per-device 锁,AHCI 自身无锁,identify() 可与 read/write 在同 controller 不同路径并发。两个 execute_command 竞态会交错 cmd_tbl 清零、build_cfis、PRDT fill、CI bit 写 → controller 执行 Frankenstein FIS。poll 循环也共享 `port->is` 清零。-smp 下 livelock/corruption 源。单 slot 假设还把吞吐永久限到 1 in-flight cmd/port。
- 建议:加 AHCI 级 Spinlock(或复用 per-port cmd_list_buf_ 作锁锚)跨 execute_command,使 build-issue-poll 序列 per-port 原子。与 AHCIBlockDevice 锁修复配套。

### signal_find_task_by_pid 返回的 Task* 锁外使用,跨 CPU exit→delete 竞态  `kernel/proc/signal.cpp:70`  [medium]

- 维度/类别:D-concurrency / registry-lock-ptr-TOCTOU-after-unlock
- 详情:函数取 g_registry_lock 找 Task 后释放锁返回裸 Task*。注释(line 72-79)警告指针只在持锁时有效且 tasks 现已会 free(DEBT-002 已修)。但 do_kill_kernel(sys_signal.cpp:84,96)与 sys_tkill(sys_linux_stubs.cpp:54,58)拿裸指针后调 signal_send(target,...),后者 deref target->state/sig_forced/sig_actions/pgid(signal.cpp:178-220)且无锁。窗口:CPU A kill(pid) 找到 Task* T;CPU B 跑 T 的 exit_current→signal_unregister_task(T)→enqueue_deferred→后 delete T。A 的 signal_send 在 B delete 后 deref T → 跨 CPU UAF。注释称 caller"只测 nullptr 或容忍 Zombie/Dead",但 signal_send 明显 deref 字段。
- 建议:(a) kill/tkill 在 registry lock 下完成 signal_send(queue_signal 不睡眠,持锁安全),或 (b) 按 target 取 per-task refcount(acquire on lookup, release after send,类比 Linux tasklist_lock + get_task_struct)。(a) 改动更小,需确认 queue_signal 无睡眠。

### lookup_in_dir 由 vfs_lookup→lookup_child 调用时不持锁,与持锁的 create/mkdir 改目录竞争  `libs/ext2/ext2_init.cpp:268`  [medium]

- 维度/类别:D-concurrency / shared-state-unlocked
- 详情:lookup_in_dir 自己 read_disk_inode 拿本地副本(副本不 race),但它扫目录块期间,若并发 create/mkdir 在 mutation_lock_ 下刚 add_dir_entry 改该目录块磁盘内容并 write_block,lookup_in_dir 读到的可能是旧块或新块(块级 I/O 无事务)。更关键:create()/mkdir()/symlink()/link() 在持 mutation_lock_ 时调 lookup_in_dir 做重名检查,而 Ext2::lookup_child(ext2_init.cpp:392)调 lookup_in_dir 时不持锁 —— 同一函数两种锁约定。两个 CPU 同时 lookup_child 同目录无锁,与写者交错仍可能扫到半提交状态。lookup_child 是 vfs_lookup 热路径,缺锁是系统性缺口。
- 建议:lookup_child 入口加 `mutation_lock_.guard()`(与 read/stat 一致);lookup_in_dir 保持内部不加锁(它已是持锁调用方的内部调用),靠调用方约定保证。

### itimer_real_tick(PIT IRQ)与 sys_setitimer(另一 CPU)非原子 RMW itimer_real_value_ns  `kernel/proc/signal.cpp:310`  [low]

- 维度/类别:D-concurrency / itimer-cross-cpu-race
- 详情:itimer_real_tick 在 PIT IRQ 对每 task 读改写 `t->itimer_real_value_ns`(减 delta / 重载 interval),仅持 g_registry_lock(保护链表遍历,不保护字段)。sys_setitimer 在 syscall 上下文用 InterruptGuard 写 self->itimer_real_value_ns —— InterruptGuard 只关本 CPU IRQ,PIT tick 可能在另一 CPU 同时跑 itimer_real_tick 改同 task。非原子 RMW:tick 读到旧 value 减完写回,正好覆盖 setitimer 刚写的新 value → 新设置计时器值丢失(用户 setitimer 后 SIGALRM 永远按旧间隔/不响)。process.hpp:315 注释声称"aligned 64-bit rw atomic (TSO); cross-CPU race vs setitimer is benign" —— 这是错的,read-decrement-write 不是单条 atomic 指令,TSO 不保证 RMW 原子性。
- 建议:对 itimer 字段访问用 per-task 锁或 __atomic compare-exchange 减法;至少把"benign"注释改成正确的危险说明。当前 busybox ping(1s ITIMER_REAL)单核稳,-smp 下 setitimer 与 tick 抢同字段会偶发 SIGALRM 错乱。

### IOAPIC 经 IOREGSEL/IOWIN 间接访问的 RMW 全程无锁  `kernel/drivers/apic/io_apic.cpp:34`  [low]

- 维度/类别:D-concurrency / lock-free shared-register RMW
- 详情:IOAPIC 寄存器通过"写 IOREGSEL(0x00) 选索引,再读/写 IOWIN(0x10)"两步访问,`base_[IORegSel/4]=reg` 与 `base_[IOWin/4]` 之间是共享硬件选择器状态。mask/unmask 还是 RMW(`read(reg) | kRedirectMaskBit`)。两 CPU 并发:CPU0 写 IOREGSEL=0x12,CPU1 写 IOREGSEL=0x10,CPU0 读 IOWIN 拿到 CPU1 选的寄存器 → 静默写错 GSI 的 mask/redirect。当前唯一调用方 irq_backend.cpp:81-82 switch_to_apic 在 BSP 单核 bring-up 跑,故未触发;但这是为未来埋的 SMP race(任何 AP 上动态 mask/unmask、热插、redirect 重路由都炸)。
- 建议:给 IOAPIC 加 Spinlock 保护所有 read/write/mask/unmask/set_redirect(Linux 用 ioapic->lock)。开销可忽略且防回归。

### setpgid 无锁写 task->pgid,killpg/pty 在 g_registry_lock 下读,跨 CPU 读到旧值  `kernel/proc/process_group.cpp:28`  [low]

- 维度/类别:D-concurrency / pgid-write-vs-killpg-walk-race
- 详情:setpgid 直接 `task->pgid = pgid`(int 写,x86 原子但无内存屏障)。killpg 在 g_registry_lock 下遍历读 `t->pgid == pgid`;pty_device 也读 task->pgid。setpgid 写与 killpg 读之间无 happens-before:setpgid 在 CPU0 把进程从组 5 改到组 6,CPU1 的 killpg(5) 可能仍把它当组 5(killpg 5 命中刚改组进程,或 killpg 6 漏掉)。int 不撕裂但"读到改前还是改后"不确定,信号投递归类错。
- 建议:pgid/sid/session_leader 读写统一在一个锁下(或用 g_registry_lock 包 setpgid 写)。当前 TOCTOU。

### TCP 临时端口用 function-local static 计数器并发自增  `kernel/net/tcp_socket.cpp:82`  [low]

- 维度/类别:D-concurrency / non-atomic-ephemeral-port-allocator
- 详情:TcpSocket::connect 自动绑定时 `static uint16_t eph = kEphemeralBase; local_port_ = eph++;` —— 函数局部 static 全进程共享且非原子。两 app 线程在不同 CPU 同时 connect()→auto-bind,可能读同一 eph → 两连接拿同本地端口(叠加无锁 cons_[] alloc,4-tuple 撞车)。UdpSocket 等价路径(udp_socket.cpp:96)用 for 循环 udp_.bind 试探,有 lock_.irq_guard() 且 bind 内查重,相对安全;TCP 既无 lock_ 保护自增也无原子,也没在 tcp_.listen 表查重。Ipv4Module::next_id_++(ipv4.cpp:105)和 TcpModule::isn_counter_ += (tcp.cpp:142) 同类,但 id/isn 撞了危害小,ephemeral port 撞了会串台。
- 建议:ephemeral 端口分配改成持 TcpModule 锁的原子/查重分配(像 UdpSocket),或用 atomic<uint16_t>;connect 前确认该端口未在 cons_[]/listens_[] 占用。

### IcmpModule::handle 遍历 raw_sockets_[] 无锁,与 RawSocket 构造/析构并发  `kernel/net/icmp.cpp:76`  [low]

- 维度/类别:D-concurrency / unlocked-raw-socket-list-iteration
- 详情:注释自承"No lock here -- the list is mutated only at socket construct/destruct (syscall path), and handle() runs single-threaded per device poll"。但"single-threaded per device poll"前提是 net_poll 唯一 poll 者,且 SMP 下 syscall 路径(RawSocket 构造注册 / close 注销)在另一 CPU 同时跑。handle() for 遍历 raw_sockets_[i] 调 on_icmp_reply,第二个 CPU 上 RawSocket 正 close()→unregister 把某槽置 nullptr:若恰在 handle 判完 !=nullptr 到调用之间被置空,读到非空但即将失效指针(随后 delete)→ UAF。低危是因 ping 用户少窗口窄,但属"看起来不会并发实则会"且作者注释已承认没锁。
- 建议:给 IcmpModule 加 Spinlock,register/unregister/handle 遍历都持锁(on_icmp_reply 内各 RawSocket 持自己锁,不嵌套)。这把锁同时覆盖 net_init.cpp g_icmp 表竞争。

### TcpModule::cons_/listens_ 被 net_poll kthread(RX)与 user-task accept/connect 无同步改  `kernel/net/tcp.hpp:244`  [low]

- 维度/类别:D-concurrency / tcp-connection-table-no-lock
- 详情:cons_[kMaxTcpCons] 与 listens_[] 是 plain 数组,find()/alloc()/set_listener() 扫/改。find/alloc 由 TcpModule::on_frame(RX 路径,resident net_poll kthread)调,set_listener 由 TcpSocket::accept/connect(user-task CPU)调。并发 alloc vs find/set_listener 同数组是数据竞争:两 caller 抢同 kClosed 槽,或 listener rebind 与状态翻转竞态。低危因 TCP 不在 smoke/release 路径(F7 单 integration gate),但 -smp 开网络立刻 race。
- 建议:给 TcpModule 加 Spinlock,find/alloc/set_listener/find_listener 持锁;尽量不进 per-segment 热路径(只锁表查找,不锁 FSM)。

### EventQueue 号称 lock-free SPSC 但无任何内存屏障,SMP 跨 CPU 读写不安全  `kernel/gui/event.cpp:14`  [low]

- 维度/类别:D-concurrency / lockfree-sp-sc-no-memory-barrier-smp(死代码,见 C 节)
- 详情:event.hpp:103-109 注释声称"lock-free SPSC,内核在 dequeue 期间 IRQ masked 所以无需额外同步"。但这是 SPSC 跨 CPU 场景:生产者(mouse ISR/keyboard listener)在 CPU0 改 tail_,消费者在 CPU1 读 tail_;反之亦然。x86 TSO 较强,但编译器仍可对非 volatile 的 head_/tail_ 重排,且与 buf_[] 的可见性顺序在编译器重排下不保证。更重要的是这是已死代码 —— dequeue 唯一消费者 Mouse::poll() 已无 caller。无论是否死代码,"lock-free 无屏障 SMP 安全"断言错误。
- 建议:鉴于已死代码(见 C 节),直接删除 event.hpp/cpp。若仍要用,head_/tail_ 改 Atomic<uint32_t>(SeqCst/Acquire-Release)。

---

## E. 资源 / 契约

### PCIDevice::bar[] 声明 uint32_t[6],64-bit BAR 高 32 位被静默截断,调用方按 uint64 读  `kernel/drivers/pci/pci.cpp:103`  [medium]

- 维度/类别:E-resource-contract / uint64-to-uint32 silent truncation
- 详情:pci.hpp:48 `uint32_t bar[BAR_COUNT];`。read_bars() 处理 64-bit BAR 时 `dev.bar[i] = (static_cast<uint64_t>(high) << 32) | (raw & BAR_ADDR_MASK_32);` 隐式截断为 uint32_t,高 32 位丢失。但调用方普遍按 64 位语义用:e1000.cpp:99、nvme.cpp:81、xhci_controller.cpp:78、ahci.cpp:300、msix_controller.cpp:62/78。QEMU 把 BAR 放 4GB 以下故不炸,但这是真实类型契约违背:任何 BAR 落在 4GB 以上的真机(常见带大显存 NVMe/GPU 的 64-bit MMIO)会静默拿到错误地址 → DMA 写错地方或 #PF。msix_controller.cpp:60 注释承认该限制但没修类型。
- 建议:把 `PCIDevice::bar` 改为 `uint64_t bar[BAR_COUNT]`,类型与调用方语义一致;真机 64-bit BAR 即正确。

---

## F. 错误处理

### AHCI::init() 返回 void,4 条失败路径静默 bail,半初始化 controller 无信号  `kernel/drivers/ahci/ahci.cpp:290`  [medium]

- 维度/类别:F-error-handling / silent-failure-void-init
- 详情:init() 有早 `return;`(BAR5==0、map_bar5 返 nullptr、setup_port 内 per-port DMA alloc 失败,setup_port 自己也返 void)。caller(init.cpp boot-disk 选择)得不到 init 失败信号 —— 不像 NVMe/VirtIO 返回 ErrorOr<void>。半初始化 AHCI(hba_mem_ 设了但某 port 的 cmd_list_buf_ 无效,或 hba_mem_ null 但 s_instance_ 已注册)使 AHCIBlockDevice::read_blocks 每次命中 `!dma_buf_.valid()` 或 `!cmd_list_buf_[port].valid()` 分支返回 IOError,根因不可见。production-kernel-init note 已记录 AHCI 解引用空单例 #GP 是真实 shipped bug;void init 是同一掩盖模式。
- 建议:签名改 `cinux::lib::ErrorOr<void> init(const pci::PCIDevice& dev)`,propagate IOError/OutOfMemory。init.cpp 已 probe is_present(),应额外把 init 失败当"无 AHCI"而非注册坏单例。

### Framebuffer::init 丢弃 map_2mb() 的 bool 返回值,映射失败后写未映射地址  `kernel/drivers/video/framebuffer.cpp:35`  [medium]

- 维度/类别:F-error-handling / unchecked-bool-return
- 详情:init() 循环 `mm::g_vmm.map_2mb(virt, phys, mmio_flags);` 直接忽略返回值。map_2mb 声明 `bool map_2mb(...)`(vmm.hpp:61),OOM/地址冲突返 false。对比同范围其它映射点都检查:LAPIC(local_apic.cpp:30 `if(!map) return false`)、IOAPIC(io_apic.cpp:27)、HPET(hpet.cpp:79)、MSI-X(msix_controller.cpp:66)。若任一 2MB 页映射失败,addr_ 仍设为 KMEM_FB_BASE+offset,之后 put_pixel/fill_rect/scroll_up/clear 全写未映射虚拟地址 → 立刻 #PF panic。危险场景:高分辨率大 framebuffer 多页在内存紧张或 KMEM_FB_BASE 被占时某页映射失败即整屏崩,且无日志提示是映射失败。
- 建议:检查返回值,失败回滚已映射页并返回错误(改 ErrorOr<void> 或至少 kprintf+halt),与 LAPIC/IOAPIC/HPET 对齐。

### make_node() `new VMA` 无 nullptr 检查,注释宣称 exceptions trap OOM 但 -fno-exceptions 下 new 返 nullptr  `kernel/mm/vma.cpp:46`  [medium]

- 维度/类别:F-error-handling / oom-null-deref-in-make_node
- 详情:operator new(arch/x86_64/crt_stub.cpp:191)委托 kmalloc,耗尽返 nullptr;-fno-exceptions(kernel/CMakeLists.txt:23)下不抛 bad_alloc。make_node 设 `node->start` 于可能 null 指针。insert() 在 vma.cpp:146(splice)与 :185(split-right)调 make_node 并立即 deref node->prev/next。slab 耗尽时 mmap/fork/clone 必然 #GP/#PF 而非 propagate -ENOMEM。注释"Heap exhaustion traps via operator new"对本 build 事实错误。
- 建议:make_node 内 `if (node == nullptr) return nullptr;`,insert() 在 make_node 返 null 时 return Error::OutOfMemory,并修正误导注释。于是 fork/clone 中丢弃 insert() 返回的 caller(另条)才有意义。

### clone() FD-table copy 同 fork() 的未检查 new FDTable()/File()  `kernel/proc/clone.cpp:338`  [medium]

- 维度/类别:F-error-handling / oom-null-deref-fdtable-file-copy
- 详情:clone.cpp:338 `child->fd_table = new cinux::fs::FDTable();` 然后 :343 `child->fd_table->set(i, copy)`;:342 `new cinux::fs::File(...)` 无 guard。与 fork.cpp:479-487 同类 OOM null-deref / null-fd-stored bug。这是 fork 代码的直接复制,修法应共享。
- 建议:两处 alloc 都 null 检查并 unwind;把 FD-copy 例程与 fork.cpp 去重。

### TmpFileOps::write 与 truncate 用 `new uint8_t[newcap]` 无 OOM 检查,noexcept 内核下 panic  `kernel/fs/tmpfs/tmpfs_ops.cpp:71`  [medium]

- 维度/类别:F-error-handling / unchecked-allocation-noexcept / oom-null-deref-tmpfs-grow
- 详情:write(:71)与 truncate(:114)`uint8_t* nd = new uint8_t[newcap];` 紧接 memcpy/memset 无 nullptr 检查。CinuxOS no-exception 内核,默认 operator new 失败抛 bad_alloc,-fno-exceptions 下即 terminate/abort = panic。对比 ext2 全程 KmBuf(kmalloc + 显式 nullptr 检查,ext2_common.hpp:27-41),tmpfs 是异类。OOM 时(newcap 可能很大)直接 panic 而非返回 ENOSPC,违反"错误走 ErrorOr"铁律。node->data 随后 memcpy 进 nd,new 失败 nd 不可用但代码继续用 → UB。
- 建议:改 kmalloc/kfree(与 ext2 一致)并检查 nullptr,失败返 Error::IOError/ENOMEM(ErrorOr 路径);或 `new (std::nothrow) uint8_t[newcap]` + 检查。两处都改。

### init_dedicated_caches 不检查 create_cache nullptr,全局 cache 静默留 null  `kernel/mm/dedicated_caches.cpp:30`  [low]

- 维度/类别:F-error-handling / init_dedicated_caches-ignores-create_cache-null
- 详情:create_cache 在 kmalloc 失败时返 nullptr(slab.cpp:289)。init_dedicated_caches 直接赋给全局无检查。内存压力下 init 跑时全局留 nullptr;后续 `new Task`/`new VMA` 经 operator new→cache_alloc(nullptr) 返 nullptr(slab.cpp:305),operator new 返 null 而非 trap —— 破坏 vma.hpp:46 文档化的"Heap exhaustion traps via operator new"失败模型,把清晰 early-boot OOM 变成深处 process 创建的静默 NULL deref。
- 建议:检查每个 create_cache 结果,nullptr 时 boot kpanic("dedicated cache OOM")使失败立即明显,或显式 fallback 到通用 kmalloc 路径。

### AddressSpace 构造 OOM 时打印 FATAL 返回半初始化对象  `kernel/mm/address_space.cpp:64`  [low]

- 维度/类别:F-error-handling / AddressSpace-ctor-half-initialized-on-OOM
- 详情:构造函数在 PML4 alloc 失败时 `kprintf('[AS] FATAL ...'); return;` 留 pml4_phys_==0。析构函数安全处理 0,但构造不抛(无异常内核)返回 constructed-but-broken 对象。caller 如 `new AddressSpace()`(进程创建)拿到看似有效指针指向半初始化地址空间;后续 map()/activate() deref phys_to_virt(0) 或 write_cr3(0) → 立刻 #GP 或静默 corruption。"FATAL" 日志是 workaround,在分配点掩盖真实 OOM 而非以 ErrorOr 上报 caller。
- 建议:提供返 ErrorOr<AddressSpace> 的工厂(或 caller 构造后立即查 pml4_phys()==0 并 propagate -ENOMEM)。至少每个 use site(map/activate/fork)加 pml4_phys()==0 guard。

### DentryCache::insert `new Dentry` 无 nullptr 检查  `kernel/fs/dentry.cpp:86`  [low]

- 维度/类别:F-error-handling / oom-null-deref-new-dentry
- 详情:line 86 `Dentry* d = new Dentry{...};` 然后 :87-90 拷贝 name 字节进 `d->name[]`。slab 耗尽时 d 为 nullptr,`d->name[i] = name[i]` null-write。dentry insert 在每次 cache miss 的 path lookup 上跑,内存压力下 lookup 崩溃。低危因需 slab 耗尽,但与高严重度站点同 unchecked-new 模式,修法平凡。
- 建议:`if (d == nullptr) return;`(或 propagate ErrorOr),让 caller 把"cache-miss-with-no-memory"当"uncached, still resolved"。

---

## G. 风格 / 行数

### pit.hpp 头注释仍称 PIT 为"square-wave generator"(mode 3),与已修为 mode 2 的实现矛盾  `kernel/drivers/pit/pit.hpp:66`  [low]

- 维度/类别:G-style / stale-misleading-docstring
- 详情:pit.cpp:59-65 已根治 PIT mode3 2x 时序 bug,改 mode 2 rate generator(注释详述根因)。但 pit.hpp:66-73 类文档仍写"Configures PIT channel 0 as a square-wave generator""Sends command 0x36 (...square wave mode)",init() 文档(:80)也写 square wave。irq0_handler 文档(:100-102)称"Every freq_hz ticks ... prints '[TICK] uptime: Ns' to serial",但实现(pit.cpp:83-97)根本没有该打印。误导后来者:若有人据头注释以为是 mode 3 重改,会重新引入已修的历史 bug。
- 建议:更新 pit.hpp 头注释与 init/irq0_handler 文档为 mode 2 rate generator、移除不存在的 uptime 打印描述,与实现对齐。

---

## 待人工复核(uncertain)

> 以下 2 条静态能确认事实,但严重度/影响/是否为有意设计需运行时或设计判断,留人工裁决。

1. **`kernel/proc/execve.cpp:374`** — execve 中途失败(sigreturn page map 后 heap VMA 失败、load_interpreter 失败)不回滚已 unmap 的旧地址空间/已 alloc 页,进程半残。**事实确凿**(L248 clear_user_mappings 原地清、L253 vmas().clear、L347/367/377/388 失败只 return 错误码),项目 note 2026-06-27 已实测复现(子返回用户态写 -12,CR2 落被 unmap 的用户栈)。**为何 uncertain**:finding 的资源泄漏归因(E 类)虚假(失败后进程死,Task 析构链回收地址空间,无永久泄漏);POSIX"execve 失败应继续原映像"在 fork+exec 模型下等同 Unix 常态(子死而非父受害);"先毁后建"是深思熟虑的单地址空间 in-place 设计(vfork 分支 L235-246 已走"建新 AS 再切",非 vfork 走 in-place)。改全程两阶段是鲁棒性增强而非 correctness fix。严重度从 medium 降到 low,处于"静态确认事实但影响需运行时/设计判断"灰区。

2. **`libs/ext2/ext2_common.cpp:193`** — ext2_trace_wild_blk 是 F-DYN-COV 临时调试桩,永久驻留 read 路径,每个 block resolve 都调用,触发时 kprintf + backtrace。**事实部分属实**:self-guard(`if (blk == 0 || blk < blocks_count) return;`)使稳态下完全 no-op,kprintf/backtrace 只在真 wild block 时触发;backtrace 由 `#ifndef CINUX_HOST_TEST` 保护但 kprintf 无 guard。**为何 uncertain**:项目 memory f-dyn-cov-race-detect.md 第 40 行明确记录这是**有意保留**(wild 根因 heisenbug 未定位,作 audit-style sentinel),非遗忘 TODO;finding 描述的"串口刷爆"只在 wild 复发时(恰恰是想要诊断时)。但有一个站得住的真缺陷:项目自家 F-DYN-COV 工具(race_detect/lockdep)都编译开关 gated(CINUX_RACE_DETECT/CINUX_LOCKDEP 默认 OFF),唯独此 sentinel 裸留生产路径且注释仍写"temporary",与"有意保留"决策自相矛盾。取决于项目把这条已记录 audit sentinel 视为可接受已知债还是应清理的债 —— 超出静态一锤定音范围。

---

*报告完。所有 file:line 均为仓库内绝对路径基线(工作目录 /home/charliechen/CinuxOS)。建议优先处理:D-high 四条(AHCI DMA 锁、VMA 锁、net 全栈锁、loopback ring 锁)+ B-critical(killpg UAF)+ A/B-high(sa_mask)。*

# CinuxOS 审计补充 — drivers-input-net 子系统 + 过度抽象新增

> 补扫两块限流缺口。drivers-input-net 整块由单 agent 扫描(15 条,**未经过 skeptic 复核**,需人工裁决);过度抽象切面新增 3 条(其余与主报告 C 类重叠)。

## drivers-input-net(15 条)

### keyboard 0xE0 扩展键扫描码无条件跳过  `kernel/drivers/keyboard/keyboard.cpp:217` [B/medium]
irq1_handler 见 0xE0 前缀直接 return("skip for now")。整个 0xE0 键集(方向键、Home/End/PgUp/PgDn、Delete、Insert、小键盘、RCtrl/RAlt)全不可见。PS/2-only 启动时光标键失效。无 tag 跟踪。建议:加 1 字节 E0 latch + E0 专用查表,或至少打 tag。

### e1000 read_eeprom 超时返 0xFFFF,read_mac 当有效 MAC  `kernel/drivers/net/e1000.cpp:63` [A/medium]
DONE 位永不置位时返回 0xFFFF,read_mac 无检查直接构 mac_=FF:FF:...:FF,init 当正常打印,start_rx 把广播地址写进 RAL0/RAH0(+UPE 置位接收所有帧),EEPROM 故障被"流量正常"彻底掩盖。建议:read_eeprom 返 ErrorOr/optional,read_mac/init 失败 fast-fail。

### mouse decode_packet 忽略 X_OVERFLOW/Y_OVERFLOW 位  `kernel/drivers/mouse/mouse.cpp:213` [E/low]
Packet0::X_OVERFLOW/Y_OVERFLOW 已定义(line 50-51)但 decode_packet 从不检查。快速移动(delta>255/包)静默截成错误值而非饱和。QEMU 难触发但违反协议契约。

### mouse init 注释"start at center"但代码置 0(左上角)  `kernel/drivers/mouse/mouse.cpp:152` [G/low]
注释与代码矛盾,误导初始光标位置诊断。

### pty_alloc check-and-claim 无锁,两 CPU 并发 open('/dev/ptmx') 抢同一槽  `kernel/drivers/tty/pty_device.cpp:442` [D/medium→high]
`if (!g_slots[i].in_use) { g_slots[i].in_use = true; wire_slot(...); }` 无 irq_guard/lock。wire_slot 之后才取 slot.lock。-smp 两 ptmx open 竞态都观测到 in_use==false → 都返回同一 master inode → 两进程共享一个 PTY 对(数据错乱、信号投错前台 pgrp)。建议:check-and-claim 前取 slot.lock.irq_guard() 或全局 alloc 自旋锁。

### ConsoleTty::input() 在 keyboard IRQ1 上下文无锁改 tty_ 状态  `kernel/drivers/tty/console_tty.cpp:90` [D/medium→high]
input()→tty_.input_char(c) 写 line_buf_/cooked_/pending_signal_、读 reader_/foreground_pgid_,而 read()/poll_events() 用本地 InterruptGuard 保护同一批成员。UP 下 OK(IRQ1 在 read 持 guard 时被本 CPU 屏蔽);-smp 下 IRQ1 在 CPU A fire 而 read() 在 CPU B 执行,InterruptGuard 只屏蔽 CPU B 的 IRQ → ring-buffer head/tail、line_len_、reader_(input 置 null 而 read 重赋值)真数据竞争。建议:input() 的 tty_ 变更 + reader_/foreground_pgid_ 访问用 Spinlock(irq_guard),与 read/poll_events 共享,同 PtySlot slave_read 模式。

### Mouse PS/2 IRQ12 与 USB inject 双生产者改同一组 statics 无锁  `kernel/drivers/mouse/mouse.cpp:184` [D/medium]
process_byte/decode_packet(PS/2 IRQ12)与 inject_usb_motion/inject_usb_absolute(xHCI MSI-X)都写 mouse_x_/y_/buttons_/prev_buttons_ 并入队 g_event_queue_。靠 usb_primary_ plain store 无 fence 保证 PS/2 提前 return;boot 窗口期 PS/2 字节在 CPU0 in-flight 而 USB 路径在 CPU1 启动 → 破 SPSC 单生产者不变量(两生产者)。建议:所有生产者串到一把 Spinlock(InputEventDevice 已为同一双生产者问题这么做),或 barrier 保证 set_usb_primary happens-before 任何 USB 传输且先确认 PS/2 IRQ 已屏蔽。

### e1000 send_packet 不等前一个描述符 DD 位即复用槽  `kernel/drivers/net/e1000.cpp:270` [E/low]
tx_tail_ 推进 + 按门铃,DD poll 在写之后。环形回绕时若前一 send 同槽未完成,status 被清零覆盖、新 cmd/buffer_addr 踩在 in-flight DMA 上。突发 ping + 小环 → 出帧损坏/丢失。建议:复用槽前查/等前一个 DD。

### tty canonical 行缓冲满时静默丢字节  `kernel/drivers/tty/tty.cpp:139` [E/low]
kLineBufSize=256,满后每个可打印字节返 kOverflow 被丢(不回显不报错),调用方忽略 InputResult。用户长粘贴/按住键看到输入被截断在 256 无提示。commit_line/cooked_push 满时也丢尾部。建议:溢出至少响铃('\a')或加大缓冲,文档化 256 限制。

### Serial::putc 无界忙等 TX-ready,无超时无 yield  `kernel/drivers/serial/serial.cpp:58` [A/low→panic-相关]
`while (!is_tx_ready()) { pause; }` 无界。putc 是 kprintf/panic 路径。LSR.TX_READY 不置位(硬件故障/串口后端卡死)→ 内核在 putc 死锁,panic 路径内死锁使 panic 不可见。建议:加有界重试计数,超时丢字节/置 stuck flag,至少让 panic 输出能进行。

### e1000 init reset 不自清时仅 kprintf 继续  `kernel/drivers/net/e1000.cpp:113` [A/low]
CTRL.RST 不自清只警告"continuing",带半复位 NIC 继续 disable-int/force-link/read-MAC/program-rings。硬件未知状态但"纸面成功"。建议:rst_done false 时返硬件故障错,net_init 干净禁用 net。

### net_init ping() 丢弃 send_echo_request 的 ErrorOr,200 轮无脑重发  `kernel/drivers/net/net_init.cpp:164` [A/low]
`static_cast<void>(g_icmp.send_echo_request(...))` 丢返回值。send 每轮失败(OOM/NIC down)仍跑满 200 轮×4 pump≈800 yield/sti-hlt 才返 got_reply=false。真实 send 错与"暂无回复"不可区分。建议:不可重试错误早 break 并上报。

### Pty::master_write 返回 count 但 input_char 可能丢字节  `kernel/drivers/tty/pty.cpp:54` [E/low]
每字节 `static_cast<void>(slave_tty_.input_char(p[i]))`,忽略 kOverflow/kSignal,无条件返回 count。破坏 write() 字节计数契约(调用方不重试尾部),隐藏输入丢失。建议:跟踪实际消费字节数返回。

### Mouse::poll 遇非 mouse 事件 return false 静默吞掉  `kernel/drivers/mouse/mouse.cpp:391` [E/low]
共享队列弹出非 MouseMove/Down/Up 事件命中 `default: return false;`。调用方把 false 当"无事件"可能停 poll,但真实事件(串进共享队列的键盘事件)已被消费丢失。建议:遇非 mouse 事件 continue 弹下一个,或返 distinct 'discarded' 状态。

## 过度抽象新增(主报告 C 类之外)

### PhysRef<Tag> 三标签只 CachePhysRef 被用,AnonPhysRef/PageTablePhysRef 零实例化  `kernel/mm/phys_ref.hpp:93` [C/low]
三向编译期所有权区分(CachePageTag/AnonPageTag/PageTableTag),但 grep 全仓只有 CachePhysRef 有实例(page_cache.hpp:73)。AnonPhysRef/PageTablePhysRef 声明后从未出现。匿名页/页表页路径直接用裸 uint64_t phys。三向划分保护单一类型=编译期保护需≥2 类型才有意义。建议:要么把匿名/页表路径迁移到对应 PhysRef,要么删两个未用 typedef+标签。

### icon_data.hpp 整个文件(build_icon/build_mask 模板+调色板+k_shell_icon/k_calc_icon)是死代码  `kernel/gui/data/icon_data.hpp:106` [C/low]
grep 'icon_data.hpp' 全仓零 #include;IconBitmap/IconMask/k_shell_icon/k_calc_icon 仅文件内匹配。约 280 行无人维护,build_icon/mask 模板(static_assert Rows==32)在自己内部实例化生成常量,但常量无人消费。建议:删整个文件,真需要桌面图标时从消费者需求重新引入。

### Scheduler 多类注册表(MAX_CLASSES=4)生产环境只注册 1 类  `kernel/proc/scheduler.hpp:114` [C/low,建议保留]
classes_[4] + pick_next_from 遍历,但生产 init() 只 register_class(&default_rr_),其余 register_class 全在 test/。pick_next_task 总遍历单元素数组。代价小(每次调度一次 1-迭代循环)+ 暴露给测试带来真实可测试性。建议:**保留**(F3-M4 定义为预期扩展,可测试性收益真实),但 MAX_CLASSES 降到 2 反映现实,注释标明多类遍历当前退化。
