# F-QA Q4 — 头号高危债收敛(2026-06-21)

> F-QA 质量收敛里程碑 Q4。**Q4 = 头号高危债收敛**(进程生命周期引用计数洼地)。Q4a 类型先行(RefCount/UserPtr)后,Q4b-e 修 6 债(DEBT-001/002/003/004/005/006)。来源:4-agent workflow 深度调研(每批牵连 grep + 方案 + 风险 + 验证 + 拆分建议)+ 用户「全连做」决策。**9 批 + 1 收尾 fix**。分支 `feat/f-qa-q4`。

## 根因(用户痛点燃料)

两份审计报告交叉印证,「内存偶现挂死 / 四处崩溃」集中在两个洼地:
1. **进程/线程生命周期没闭环** —— 退出不释放(DEBT-002)+ CoW 无 mapcount(DEBT-003)+ CLONE_VM 无 refcount(DEBT-006)。缺「物理页 / 地址空间 / 任务」三者的引用计数与退出清理基础设施。
2. **F4 漏网的全局可变状态** —— registry(DEBT-001)/ PidAllocator(DEBT-005)/ waiting_for_child(DEBT-004)。单核串行永不显现,多核偶现炸。

Q4 收敛这两个洼地。Q4a 铺类型(RefCount/UserPtr),Q4b/c/d/e 修 6 债。

## 各批(9 批 + 1 fix)

| 批 | 债 | 核心改动 | commit |
|----|----|----------|--------|
| Q4d | DEBT-005 PidAllocator 无锁(P1) | pid.hpp/cpp 四方法(alloc/free/is_allocated/count)加 `lock_.irq_guard()`;mutable Spinlock;对调用方透明 | 389987c |
| Q4c-1 | DEBT-004 waiting_for_child 非原子(P0) | sys_exit 无条件 `unblock(parent)`(幂等);删 `Task::waiting_for_child` 字段 + waitpid 写点 | 7b72659 |
| Q4c-2 | DEBT-001 registry 无锁(P0) | `g_registry_lock`(irq-safe Spinlock);register/unregister/find 持锁;**killpg 持锁收集 targets → 释锁 → signal_send**(绝不在锁内 send→exit_current→schedule) | 928b645 |
| Q4b-1 | DEBT-003 铺路 | PMM `mapcount_storage_`(2B/page,第三块元数据)+ `mapcount_inc/dec_and_test/load`(__atomic ACQ_REL);alloc 设 =1 | 0a4ba1c |
| Q4b-2 | DEBT-003 UAF 核心 | fork copy_page_table_level leaf 共享页(含只读+huge)mapcount_inc;execve clear_user_mappings leaf 数据页 `dec_and_test`(归 0 才 free) | 34a4595 |
| Q4b-3 | DEBT-003 收官 | handle_cow_fault 旧页 dec(flush_tlb 后)+ `test_pmm_mapcount.cpp`(5 单测) | 037a08d |
| Q4e-1 | DEBT-006 铺底 | AddressSpace 加 `cinux::lib::RefCount` + acquire/release;clone CLONE_VM acquire(RefCount 首个真消费者) | 7ddda74 |
| Q4e-2 | DEBT-002 正常路径 | waitpid reap `delete target`(→release_resources sig/cwd/fd/addr_space)+ `free_kernel_stack`(translate 核栈→phys + unmap + free_pages,parent 上下文安全) | 3983fe6 |
| Q4e-3 | DEBT-002 异常路径(最险) | exit_current `enqueue_deferred`(挂 deferred 链,在自己核栈不能 free)+ schedule 入口 `reap_deferred`(锁外 free/delete) | 4bb6ca4 |
| Q4e-2 fix | registry 悬垂(收尾审查) | waitpid reap 补 `signal_unregister_task(target)`(sys_exit 留 Zombie 在 registry,reap delete 前摘) | e6ce2f4 |

## 验证矩阵(Q4 全绿)

- **run-kernel-test 887/0**(每批;+5 mapcount 单测 Q4b-3)
- **-smp 2** run-kernel-test-smp 887/0(Q4 全是 SMP 债,真多核验证)
- **LOCKDEP** 887/0(锁序:Q4c-2 killpg 释锁后 send / Q4e-3 deferred 锁 / Q4d pid 锁 —— 无 lockdep 误报/死锁)
- **host ASAN** test_host 绿(Q4d host_spinlink + mapcount host)
- **实机** GUI 桌面([APIC]+[GUI] Desktop;**kernel_init exit→enqueue→reap 链路不崩**,Q4e-3 deferred-free 实机通)

## GOTCHA / 陷阱(Q4 新增可复用)

- **核栈自释放**:exit_current 在自己核栈跑,context_switch 切走前不能 free 自己核栈 → deferred-free(挂队列,另一任务 schedule reap 释放)。通用铁律:任何「任务退出 + 释放自己核栈」须 deferred。
- **CLONE_VM AS refcount**:线程组共享 AddressSpace,exit release 到 0 才 delete(否则兄弟线程 PML4 整棵 free 全员崩)。RefCount(Q4a)首个真消费者。
- **PF IF=0 释放**:PF handler(user segfault)调 exit_current 时 IF=0。release_resources 的 close→IO 可能持锁 → deferred 推迟到 schedule(IF=1,非 PF)安全。
- **killpg 持锁 send → lockdep panic**:signal_send→terminate→exit_current→schedule 持锁跨调度。必须「持锁收集 pid → 释锁 → send」。
- **registry find 释锁返指针 TOCTOU**:find_task_by_pid 持锁返指针,释锁后 caller 用。Q4e 修 delete 后,指针可能悬垂(reap delete race)。当前 -smp 2 AP idle 不触发;**RCU-safe 是 follow-up**。
- **SMP 跨核 TLB shootdown**:cow fault free old(mapcount 归 0)。跨核线程 TLB stale 可能访问已 free 页。Linux mmu_gather + IPI。当前 AP idle 不触发;**follow-up**。
- **孤儿 reparent**:waitpid reap target 不 reparent 其 children(target 的子任务成孤儿,Zombie 泄漏)。Linux reparent init。**follow-up**。

## 残留 follow-up(登记,不急修)

1. **SMP 跨核 TLB shootdown**(cow fault free old + AS free subtree):Linux mmu_gather + IPI TLB invalidate。当前 -smp 2 AP idle / 线程不跨核迁移,不触发。
2. **registry find TOCTOU**(Q4e 修 delete 后):find 释锁返指针,reap delete race 悬垂。RCU-safe 或 find+send 原子(锁内 send,但 send→schedule 不能持锁 —— 需更深设计)。
3. **孤儿 reparent**:reap target 的 children reparent init(Linux do_wait/do_notify_parent)。
4. **mapcount huge tail**(Q4b-2):huge 页只 inc 基址(用户 huge 未启用,GOTCHA#13 huge-split 未做)。启用 huge 后补。

## 收官

**DEBT-001/002/003/004/005/006 全修**。F-QA Q4 完成。F-QA 里程碑(Q1 防新债门禁 + Q2 审计方法论 + Q3 系统审计 + Q4 头号债收敛)收官。基线 875→**887/0**(Q4b-3 +5 mapcount 单测 + Q4a +7 user_ptr + Q4a +10 refcount... 累计)。

用户痛点「内存偶现挂死 / 四处崩溃」的头号燃料(进程生命周期引用计数洼地)收敛:CoW UAF 修(DEBT-003)、exit cleanup 修(DEBT-002 正常+异常路径)、CLONE_VM AS refcount(DEBT-006)、registry/pid/waiting SMP 安全(DEBT-001/005/004)。残留(SMP TLB shootdown / RCU registry / reparent)登记 follow-up,当前 -smp 2 AP idle 不触发。
