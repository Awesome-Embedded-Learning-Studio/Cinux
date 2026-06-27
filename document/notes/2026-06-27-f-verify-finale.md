# F-VERIFY 动态验证与并发检测基建 — 收官（核心完成,待 PR）

> 横切里程碑 F-VERIFY（与 FO/F-INFRA/F-QA/F-CLN 同档）。分支 `feat/f-verify`，18 commit。
> 起源见 audit memory `debugging-audit-dynamic-coverage-gap` + PLAN「🔄 F-VERIFY」段。
> 本笔记收一个版本：核心 M0/M1/入口/M3/M4-TSAN/M5/M6-1/2 全 done，KCSAN-lite/M2/M6-余作 follow-up。

## 起源（为什么开这个里程碑）

2026-06-27 一个 15-agent audit 抽了 **165 个调试时间坑** + 审了 5 维基建。核心结论:2026-06-21 那轮 14 维度静态债务审计量的是「代码写得对不对」,但所有超时调试都是「**代码有没有真被跑到、机制有没有真生效、崩了能不能一眼看懂**」——动态/环境性另一根轴,静态债表天生瞎。165 坑根因 top:机制没真生效 27 / 并发没压到 22 / 基建≠生产 19 / 规格看错 17 / 内存 UAF 14。

用户拍板:单独开 F-VERIFY,重锤 SMP + 并发检测,补测试矩阵 + 测试整理,**目标把多会话 forensics 变一次 CI 红灯**。

## 核心 landed（18 commit,全绿）

| 批 | 战果 | commit |
|----|------|--------|
| **M0** 零风险赢点 | RUN_ALL_TESTS 虚报 PASS 修 / test_f9 OSFXSR·OSXMMEXCPT 机制回读 / 22 处 `0x%p` 双前缀清扫 / check_memory_layout.py 接 CI。砍 3 条 audit 误判(kMaxCpus static_assert 早存在 / check_test_count 已 grep-a / unsigned-overflow sanitizer 是 Clang-only GCC 不认) | 4720138 a900a7f 73944b4 1f6de89 |
| **M1** 测试矩阵 | 6-agent workflow 审 47 子系统 × 6 维度 = 282 格 grep 坐实;36 假测 + 38 机制位登记。量化坐实:host-integration 真码链接 **37/47 ❌**、QEMU-SMP **47/47 ❌(空转)**、机制回读 ~27/47 ❌ | 8793a90 |
| **统一入口** | `run-kernel-test-all`(单核→-smp 2 一条命令,防忘跑 -smp 变体)+ CI/文档切默认 | b43b273 |
| **M3** SMP 唤醒 | M3-1 真 ACPI init(-smp 2 检出 2 CPU)+ M3-2 gated 钩子(ApSelfcheckFn,fn=null 生产字节不变)+ AP 真醒 + CR4/EFER/LSTAR 回读 PASS。**破解 47/47 SMP 空转**;LSTAR==0 #DF 类(F5-M5 GOTCHA)CI 立抓 | 141b093 03201e8 214c43a |
| **M4-TSAN** 并发检测 | `CINUX_HOST_TSAN` option + CI job(补 lockdep 看不见的原始数据竞争);修 test_sync_concurrent 紧密自旋 TSAN 病理(加 yield)。60/60 零 race | c76f98e |
| **M5** 真 fork/CoW 压力 | M5-1 真 `copy_page_table_level` CoW 标记测试(消 M1 头号假测,挖出 level 语义坑已修)+ M5-2a `-smp forktest` CoW 压力门(真用户 fork+CoW 50 迭代 races=0,替手动 hack)+ M5-2b 跨核(ApSelfcheckFn 返 bool→AP 进调度器,forktest child 跨核跑 races=0)。**F10 四修在 -smp 真跨核压力下稳,未挖出跨核 UAF** | 896f93d c398ffe 46fe2fa |
| **M6-1/2** 故障可观测 | M6-1 #PF debugcon 首故障捕获(解码 err P/W/U/RSV/I,防嵌套丢首故障)+ M6-2 CoW 解析失败 dump phys+mapcount(lock-free PTE 走读)。CoW 类崩溃一眼定位 | b08a135 6123905 |

## 四件事全部落地有产出

- **SMP 安全**:M3 破解 47/47 空转 + M5-2b 跨核 CoW 压力 races=0。
- **并发安全**:M4-TSAN 数据竞争检测器(+ F-QA Q4 早给已知 race 加锁)。
- **出问题立马知道哪**:M6-1 首故障 err 解码 + M6-2 CoW mapcount 一行定位。
- **挖 bug**:M5-1(挖出 level 语义坑,已修)+ M5-2a/2b(-smp 真跨核 fork+CoW 50 迭代 races=0,F10 四修稳)。

## 怎么用新门禁

- **默认验证**:`cmake --build build --target run-kernel-test-all -j$(nproc)`(单核→-smp 两 leg,-smp leg 真 boot AP + 回读)。
- **并发检测(本地)**:`cmake -B build-tsan -DCINUX_HOST_TSAN=ON -S .` → test_host。
- **-smp CoW 深挖(本地,需 musl sysroot)**:`CINUX_NO_KVM=1 cmake -DCINUX_MUSL_HELLO_SMOKE=ON`,run-kernel-test-smp 跑 /hello + /forktest(ITERS=50 门速度;`FORKTEST_ITERS=300 bash build-forktest.sh` 深挖)。

## 验证（PR 前）

- `run-kernel-test-all`(smoke OFF,TCG)两 leg **955/0** + M3-2 AP wake PASS。
- `test_host` **60/60**。
- `CINUX_HOST_TSAN` build test_host 60/60 零 race。
- smoke ON -smp:`FORKTEST iters=50 races=0` + AP 进调度器跨核。

## 关键设计 / GOTCHA

- **gated 钩子范式**:生产 SMP 代码(ap_main,GOTCHA 最密文件)用 default-off 分支接入测试,fn=null 生产字节不变——可复用模式。
- **ApSelfcheckFn 返 bool**:true=AP 进调度器(smoke 跨核)/ false=halt(suite-only)。测试内核 fn 决定 smoke on/off,生产文件不耦合测试标志。
- **`run-kernel-test-smp` ≠ 测了 SMP**:带 -smp 2 但不 boot_aps = 空转。M3 前所有"SMP 绿"都不证明 AP 路径。统一入口现两 leg 都跑。
- **「integration test」≠ 链真码**:CinuxOS 的 add_cinux_integration_test 允许多源,但很多只列了 unit test_X.cpp(镜像)。命名误导。M2 要么链真码要么标注。
- **aggregate 不能从 volatile 源拷贝**:`ApSelfcheckResult r = *volatile_slot` 编不过 → 逐字段读。
- **copy_page_table_level level 语义**:level 1=PT(叶),4=PML4 根。fork 外层循环遍历 PML4 + level 3 per PDPT;调 PML4+level 4 等价(M5-1 挖出)。
- **GCC 无 unsigned-overflow sanitizer**:Clang-only;unsigned-wrap 防护须显式 checked 运算(DEBT-020 已注)。
- **TSAN + 紧密自旋锁病理**:无 yield 的 spinlock 在 TSAN 100x 开销下饿死;加 yield(真 spinlock 本就该 cpu_relax)。

## audit 误判澄清（核实后砍,不盲信）

audit 宏观根因归纳(165 块/8 家族)可信且高价值,但**逐条行动建议必须逐条核实**——本里程碑砍了:kMaxCpus static_assert(早存在)、check_test_count grep-a(早用)、unsigned-overflow UBSAN flag(GCC 不认)、test_sync_concurrent"紧密自旋"(其实是 TSAN 病理不是 bug)。宏观可信,微观必验——这正是 F-VERIFY 要建立的"不盲信绿灯"文化。

## Follow-up（留 PR 后,ROI 不如核心）

- **M4 KCSAN-lite**:内核 TOCTOU 检测。**价值降低**——F-QA Q4 已给头号 race(registry/pid/waiting_for_child/mapcount)加锁/原子,现在是"没已知 race 可抓"的投机插桩,且碰热路径。
- **M2 测试整理**:消 36 镜像副本链真码(host 测真码)。有长期价值但依赖提取易坑(fork.cpp/scheduler.cpp host 链接牵连多)。
- **M6 余**:用户栈 dump(需 SMAP stac/clac)+ page-owner tag。中等,F10 用户栈 bug 已修,属投机可观测。
- **test-matrix.md 升 ✅**:各批补的测试回头把 ⚠️/❌ 格升级(矩阵现是 M1 审计时快照)。

## push/PR

用户控制。feat/f-verify 18 commit 待 push。commit 规范:`<type>(f-verify): <中文简述>`,纯描述,不带 Co-Authored-By / AI 署名。
