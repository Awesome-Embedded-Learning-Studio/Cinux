# Codex 等价命令（粘贴式 prompt）

> Codex 无 slash 命令；复制下列整段进对话即可。Codex 会自动读 AGENTS.md。

## /resume
读 document/ai/PLAN.md 与 document/ai/DIRECTIVES.md，跑 git log --oneline -10。三行回答：①你在哪(最近✅批+commit) ②下一步(🔄批+范围,grep 定位文件) ③相关陷阱。git 与 PLAN 不一致先指出。只读不改。

## /status
读 document/ai/PLAN.md，紧凑打印批表(状态/范围/commit/测试)+最近一条 git log --oneline -1。只读。

## /next [批-id]
读 PLAN.md 与 DIRECTIVES.md。为「批<id,留空=🔄NEXT>」产出脚手架：①范围 ②触及文件(grep 绝对路径;批不存在报错停) ③ErrorOr 签名草案 ④完成门 run-kernel-test 全绿 ⑤提交草案(无 Co-Auth) ⑥gotcha。停下等确认,不开改。

## /done
跑 cmake --build build --target run-kernel-test -j$(nproc)(别用 run/test_host 当门)。passed 且 0 failed=绿。
- 红:列失败项,不改 PLAN,不提交,给方向后停。
- 绿:①更新 PLAN.md(批标✅+commit短hash+测试数,挪 NEXT,必要时同步 ROADMAP) ②起草提交 `<type>(<scope>): <简述>` 无 Co-Auth,不自动提交 ③写 document/notes/<date>-<topic>.md(正式发布质量:背景/目标/设计/决策/陷阱/验证,非 diff 复述) ④报告改动+测试+建议 git 命令。

## /roadmap
读 document/ai/ROADMAP.md,紧凑打印里程碑树+依赖瓶颈,指出"当前焦点之后下一个可启动的里程碑/Feature"。只读。

## /milestone [M-id]
读 ROADMAP.md 与 DIRECTIVES.md。为「<id,默认下一未启动里程碑>」propose:①目标范围 ②批分解(每批≈一commit+完成门) ③触及子系统/文件(grep) ④与架构不变量契合点 ⑤风险/依赖。草案停下等确认;确认后写入 PLAN 并在 ROADMAP 标🔄。不开改。

## /audit
跑 git --no-pager diff --stat 与 git --no-pager diff。对照 DIRECTIVES 逐条查:命名/注释/无异常/ErrorOr用法/子模块边界/syscall翻译边界/提交格式(无Co-Auth)。列违规+定位+建议。只报告(除非要求改)。

## /preflight [目标]
读 document/ai/PLAN.md、DIRECTIVES.md、QUALITY-GATES.md。针对「目标」做改前预审:①范围/非范围 ②触及文件与调用方(`rg`) ③风险等级(R0-R5) ④命中的风险域 ⑤必须守住的不变量 ⑥验证矩阵 ⑦需同步文档。若属新里程碑/跨子系统大改,停下等确认;否则给可执行批计划。

## /quality-review
跑 git status --short、git --no-pager diff --stat、git --no-pager diff。读 QUALITY-GATES.md,按 G0-G8 输出 pass/fail/n/a;高危 findings 先列 file:line;给最小补救建议。只报告,除非明确要求修。

## /infra-audit [维度]
读 document/todo/quality/audit-guide.md 与 document/todo/quality/debt.md。按「维度,留空=下一待审维度」执行深度审计:①列搜索式 ②读真实代码取证 ③写 reports/<date>-<dimensions>-audit.md ④登记候选债务(含位置/根因/触发/修复/验证) ⑤更新 debt.md 与 quality/README.md 审计进度。默认只登记不修复。

## /fix-debt [DEBT-NNN]
读 document/todo/quality/debt.md、document/ai/QUALITY-GATES.md、document/todo/quality/audit-guide.md。对指定债务 propose 修复批:①根因复核 ②触及文件/调用方 ③设计与同步策略 ④测试计划 ⑤文档同步 ⑥commit 草案。停下等确认;确认后按一债一批执行,绿后更新 debt.md+notes。

---

## 任务交接：F-USABILITY 修 #DF 栈溢出 + SIG21 busybox SIGTTIN（粘贴给 codex）

> 把下面整段粘贴给 codex，自洽含分支/已完成/根因实证/修法约束/验证/规范入口。

你在 CinuxOS 仓库（C++17 内核，主分支 `main`）。**先读规范再动手**：`CLAUDE.md` + `document/ai/DIRECTIVES.md`（架构铁律）+ `document/ai/CODING-TASTE.md`（标识符+注释英文、私有 `_`、常量 `k`、提交前 clang-format）+ `document/ai/PLAN.md` 顶部「🔄 F-USABILITY」段 + `~/.claude/plans/humming-floating-wozniak.md`。

### 背景（已完成，不要重做）
分支 `feat/f-usability`（从 main `6ba4a88`，**5 commit 未 push**：`e12a386`/`ed7849e`/`13388d1`/`439cc75`/`6629247`）。symlink follow 已闭环（`InodeType::Symlink` + `Ext2::readlink` fast-symlink 判据 `i_blocks==0` + `FileSystem::lookup_child` + 新 `kernel/fs/vfs_lookup.{hpp,cpp}` component walk+follow+环检测 + execve main+interp follow）。Buildroot rootfs（`build/buildroot/output/images/rootfs.ext2`，Bootlin musl busybox）已 boot 到 busybox init（`CinuxOS Buildroot init: userspace up`）+ fork /bin/sh + jumping user mode。**但 ash 无提示符、不能交互**，卡两个独立 bug。working tree 干净（诊断 log/workaround/增栈均已回退）。

### Bug 1（头号）：#DF 栈溢出
sh 运行时 PIT 时钟中断 double fault：
```
==== EXCEPTION: #DF (vector 8) ==== / KERNEL PANIC Double Fault (error=0)
RIP=0xFFFFFFFF81003B8D (= irq0_stub, interrupts.S:427)  RSP=0xFFFF80000B20FFD0
```
**确认栈溢出**：`STACK_PAGES` 4→8（16→32KB）消除 #DF（实测）。**严禁增栈 workaround**——Linux 栈 8KB 够，CinuxOS 16KB 溢出 = 调用链栈过深 bug。`STACK_PAGES` 已回退 4。
**已排除**：①单函数大栈帧（生产 big_kernel `-Wframe-larger-than` 净，只 test 超限）；②`handle_pf` sti-in-handler→PIT 嵌套（page_fault.cpp 无 sti，排除 sys-ping-df 模式）。
**真根因方向**：嵌套调用链累计 + PIT 中断在该 task 栈叠加。嫌疑：demand-page/CoW（`handle_pf`+ext2+mm CoW）、`execve`/`elf_load`、`read` 链。Buildroot busybox **动态 musl** 启动期大量 demand-page/CoW 压深栈。
**真修**：①instrumentation——PIT 入口打印 RSP 与 current `kernel_stack_top` 差（剩余栈深），抓最深时刻+backtrace（FO KALLSYMS 就绪）；②定位最深函数/链，减栈（大局部改堆 `unique_ptr`/kmalloc、拆深嵌套、递归改迭代）；③验证 16KB（`STACK_PAGES=4` 不动）下 #DF 消 + ash 到提示符。**不改 STACK_PAGES / handle_pf 保持 cli**。

### Bug 2：SIG21 busybox 自发 SIGTTIN（#DF 修完再看，独立）
sh `kill(0, SIGTTIN)`（`do_kill_kernel` 诊断 `[KILL] caller pid=2 -> pid=0 sig=21`），default stop。`console_tty_ioctl` 的 `kTiocsctty`/`kTiocspgrp` 加 `[TTY]` log **全程空**——busybox 没调。试过（已回退，无效）：`kTiocsctty` 设 `foreground_pgid=current->pgid` + `/dev/tty` fall back `/dev/console`。根因：console 模式无 PTY（`controlling_tty=-1`），busybox init/sh 控制终端作业控制协议未满足，ash 自检 raise SIGTTIN。需搞清 busybox ash job control 为何 raise（session leader/控制终端/tcgetpgrp），对症补 CinuxOS 语义（参考 F3-M3 进程组 + F10-M3 PTY）。

### 验证（共用）
```bash
cmake -B build-console -DCINUX_GUI=OFF -DCINUX_BUILD_TESTS=ON \
  -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF -DCINUX_BUSYBOX_SMOKE=OFF \
  -DCINUX_ROOTFS_BUILDROOT_IMG="$(pwd)/build/buildroot/output/images/rootfs.ext2" -S .
cmake --build build-console -j$(nproc)   # 改公共头/接口编两次（KALLSYMS 两阶段）
timeout 90 cmake --build build-console --target run > /tmp/boot.log 2>&1
grep -anE "/ #|~ #|BusyBox|userspace up|#DF|SIG21|default stop" /tmp/boot.log
```
成功：ash 提示符（`/ #`/`~ #`）或 BusyBox banner，无 #DF/SIG21。**回归门**（改公共接口/头必跑）：`timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 应 1101/0 + `cmake --build build --target test_host` 100%（build/ 关 smoke + `-DCINUX_GCC_TOOLCHAIN=OFF` 防 GCC 自举 smoke 拖超时）。QEMU 一律 `timeout` 包裹；多会话 `-vnc :0` 撞车时临时 sed `:0→:5` 跑完 `git checkout` 还原；输出 `>/tmp/x.log`+grep，别 cat 全文。

### 纪律
一批一 commit 一验证（绿才提交）；msg `<type>(<scope>): <中文简述>` 无 Co-Auth；commit 你做，push/PR 归用户；源文件 500 行软限；Error 经 `cinux::lib::ErrorOr<T>`（Cinux-Base 子模块）；根目录无 Makefile 一律 `cmake --build build --target`；完成一批写 `document/notes/<date>-<m>-<b>.md`。

接到后先确认：读 PLAN.md F-USABILITY 段 + `git log --oneline -6`（在 `feat/f-usability`，HEAD=`6629247`），从 Bug 1 instrumentation 起。
