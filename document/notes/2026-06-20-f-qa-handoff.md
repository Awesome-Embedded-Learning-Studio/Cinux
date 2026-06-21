# F-QA 里程碑交接文档（2026-06-20）

> 给下一个接手 **F-QA 质量收敛与加固** 的 Claude/AI。配合 `/resume`（读 `document/ai/PLAN.md` + `git log`）食用。本文补充 PLAN 之外的"当前状态 + 阻塞 + 下一步具体怎么做 + 陷阱"。

---

## TL;DR（你在哪）

- ✅ **Q1 已合 main**：PR #25 **MERGED** @ 2026-06-20T11:03:41Z（squash commit `103903e [enhauncement]: quality enhancement (#25)`），875/0 全程绿。Q1 的 8 个 commit 内容已全部进 main。
- 当前分支 **`feat/f-qa-q2`**（Q2 起点，从 main `103903e` 拉，目前只有本交接文档一个 commit）。`main` = `origin/main` = `103903e`（干净）。
- 旧分支 `feat/f-qa`（`2a24260` re-trigger + `3c2904d` grep fix）已 squash 合 main，可删（`git branch -D feat/f-qa` + `git push origin --delete feat/f-qa`）。
- **下一步：Q2 deterministic 审计方法论**（见本文「下一步 Q2」段）。

---

## F-QA 是什么

横切质量里程碑（像 FO / F-INFRA，插 F5 驱动前）。来源：2026-06-20 一个 **6-agent workflow**（Linux 全谱 / 现代 C++ 工具链 / 维度 gap / 仓库实证 / 综合 / 对抗 critic）的收敛结论 + 用户的四个决策。两条腿：

- **防新债**：工具门禁 + deterministic 审计方法论 + 类型不变量（RefCount/UserPtr）
- **抓存量**：系统审计坐实潜在问题（DEBT-015/016/017 已是 Q1 顺手抓到的）
- **修头号高危债**：进程生命周期引用计数洼地（DEBT-001/003/004/005…）

**用户四决策（铁律，别违背）**：
1. 强检查可以上，但**只要真抓问题的**（仪式性 / 好看不中用的工具是废的）
2. **禁异常铁律优先**；现代 C++ 属性（`[[nodiscard]]` 等）能用就用、和铁律冲突就放弃
3. 新审计维度（D13/D14）同意加
4. **全包 Q1-Q4** + RefCount/UserPtr **都入本 F**

---

## 已完成 Q1（8 commit，875/0 全程绿）

| 批 | Commit | 产出 |
|----|--------|------|
| 批0 立项 | `8bdfaa2` | 质量文档分层（DIRECTIVES L9 / QUALITY-GATES / prompts / todo/quality）+ ROADMAP/PLAN 标 F-QA |
| Q1-1 门禁 | `3f31a3e` | 7 个 `-Werror=`（vla/implicit-fallthrough/undef/duplicated-branches/duplicated-cond/logical-op/format-security）+ `pipe.cpp` duplicated-branches 修；`-Wframe-larger-than` **撤回**（9 处真栈帧债）→ **DEBT-015** |
| Q1-2 nodiscard | `7825710` | `ErrorOr` class `[[nodiscard]]`（**子模块** `Cinux-Base` `af80a68`）+ big_kernel_test/test/ 加 `-Wno-unused-result`（生产零忽略，忽略全在 test）→ **DEBT-016** |
| Q1-3 CI matrix | `20b624b` | ci.yml kernel-tests 扩成 `{Debug,Release}×{none,ubsan,lockdep}` 6 组合并行 |
| Q1-4 计数门禁 | `d582a72` | `scripts/check_test_count.sh`（baseline 875，防 silent test loss） |
| Q1-5 host ASAN | `f938335` | `test/CMakeLists` 加 `CINUX_HOST_ASAN` option（ASAN+UBSAN+gcov）；**首跑即抓 ring_buffer OOB** → **DEBT-017**；ci.yml host ASAN 暂不启用（等 DEBT-017 修） |
| PLAN 收官 | `98c1f82` | PLAN F-QA 段 Q1 ✅ 批表 |
| CI fix | `3c2904d` | `check_test_count.sh` grep 加 `-a`（serial.log ANSI 被判 binary，GOTCHA#2） |

**Q1 抓到的 3 条真债（不只是防新债，兑现了"抓存量"）**：
- **DEBT-015**（P1）：9 个 syscall handler 栈帧 4-8KB/16KB 栈（`char[PATH_MAX]` 置栈）—— `-Wframe-larger-than` 抓到。
- **DEBT-016**（P2）：32 处 test fixture 忽略 `ErrorOr`（`TEST_ASSERT_TRUE` 宏 `return;` 非 void 不兼容）—— `[[nodiscard]]` 抓到。
- **DEBT-017**（P1，**最该优先修**）：`third_party/Cinux-Base/include/cinux/ring_buffer.hpp:73` `RingBuffer::push_batch` **global-buffer-overflow**（production pipe/keyboard 在用，单核串行潜伏）+ 2 处泄漏 —— host ASAN 抓到。

---

## ✅ Q1 已合 main（原 CI 阻塞已解决）

PR #25 re-trigger（push 空 commit `2a24260`）后 CI 6-config 全绿，用户已 merge。**交接时无阻塞**。

> 历史教训（留给后人）：Q1-4 的 `check_test_count.sh` 首版 grep 没加 `-a`，serial.log 含 QEMU ANSI 被判 binary → 6 config 全 fail（但 run-kernel-test 本身全过）。这是 GOTCHA#2（ANSI → `grep -a`）的再犯——本地用干净假 log 测不到，只能 CI 实跑暴露。已修（`3c2904d`，合 main）。**写解析 CI 产物的脚本时，默认 `grep -a`。**

---

## 下一步 Q2：deterministic 审计方法论

**目标**：把 `document/todo/quality/audit-guide.md` 从"叙述式红线"改造成 per-维度 **deterministic 锚点 checklist**，让任意两轮审计产出可比较（根治"发散"）。这是 F-QA 防新债的方法论基座。

**必做项**：
1. **audit-guide 每个维度改造成**：固定 rg 锚点表 → 跑完才能读码 → 必查不变点逐条 pass/fail/n/a（带 file:line 证据）→ 证据门槛（维度 done 判据）→ 闭环动作。**给 D4 进程/线程生命周期做完整样板**（前轮 workflow 已产出一个样板，见调研结论）。
2. **新增 D13（资源配额/非堆）+ D14（整数溢出/边界）** 为独立维度。**关键：锚点必须先 `rg` 校准到真实 API 符号再进模板**——前轮 critic 掀翻的最大错误就是 D13/D14 锚点用了虚构符号（`alloc_fd`/`request_irq` 在仓库零命中，真实是 `FDTable::alloc`/`PidAllocator::alloc`），会让首轮审计按假符号跑空。
3. **D5（调度/迁移）+ D8（测试覆盖）保留"先读码后定锚点"例外**——这两个维度的有效 rg 需要先读调用链才能写（GOTCHA#25/26 的迁移路径），不能一刀切"先锚点后读码"。
4. **证据门槛防注水**：用"命中行号清单进表"（机器数）+ "非债确认必须覆盖 N 条不变点反例"，而非"文件数 + yes/no"或"≥5 非债确认"（后者可凑数）。
5. **去重规则机械化**："DEBT 已在某维度登记的，新维度只允许新增该 DEBT 未覆盖的不变点列"，而非"判断主因是状态机还是并发"。
6. **锚点表回归门禁**：下一轮 rg 重跑命中数与上轮 diff>0 必须在报告显式说明（防"代码已变锚点表未变"的伪可复现）。
7. **QUALITY-GATES.md ↔ audit-guide 范式统一**（G0-G8 已是 pass/fail/n/a 范式，audit-guide 采用同款）。

**验证**：Q2 是文档为主，低风险；改完 audit-guide 自检（D4 样板能跑通 rg）。

---

## 后续 Q3 / Q4 概要

- **Q3 系统审计抓潜在问题**（零风险，只读）：用 Q2 的新方法论审待审维度 D4/D5/D6/D7/D11 + 新 D13/D14；每轮 `reports/<date>-<dims>-audit.md` + 登记 DEBT-018+；把 D2/D3 已有的 9 条 ⚠️待压测**坐实或降级**。
- **Q4a 类型不变量**（碰子模块）：`RefCount`（Cinux-Base，对齐 Linux `refcount_t` 饱和语义）+ `UserPtr`（kernel/lib，对齐 sparse `__user`）。顺带给 `ErrorOr` class 的 `[[nodiscard]]` 已经在 Q1-2 加了。
- **Q4b-d 修头号高危债**（高风险，碰 fork/execve/PF）：DEBT-003 CoW mapcount（RefCount 首个消费者）→ DEBT-001 registry 锁 → DEBT-004 waiting_for_child → DEBT-005 PidAllocator 锁。**一债一批一 commit 一验证**，每批 `-smp 2` + UBSAN/LOCKDEP。
- **Q4e exit cleanup + AddressSpace refcount**（**最险，DEBT-002/006 联动**）：做到时**单独 propose**，评估是否拆独立 F。

---

## 关键约定 / 陷阱（必读，别重踩）

- **子模块流程**（Cinux-Base，Q1-2/Q4a/DEBT-017 都碰）：子模块在 `main` branch（非 detached），commit 安全。流程：子模块内 `git commit` → 主仓库 `git add third_party/Cinux-Base`（bump ref）→ **push 时先 push 子模块再 push 主仓库**（否则 CI checkout submodule 找不到 ref）。
- **工作流**：**commit 我做，push / gh pr create 由用户控制**（memory `always-use-pr`）。一批一 commit 一验证，绿才提交；commit msg `<type>(f-qa): <中文简述>`，**不带 Co-Authored / AI 署名**。
- **验证**：`timeout 40 cmake --build build --target run-kernel-test -j$(nproc)`（QEMU 真内核 ~875 项）；改公共接口/InodeOps/mock 后补 `cmake --build build -j$(nproc)` 全量或 `make test_host`（CI 盲区：run-kernel-test 不编 test/unit/ host 单测）。
- **GOTCHA#2**（Q1-4 踩过）：测试 log 含 ANSI escape → grep 当二进制静默，**一律 `grep -a`**。`check_test_count.sh` 已修（`3c2904d`）。
- **check_test_count.sh baseline = 875**（kernel/CMakeLists 那个 `-Wno-unused-result` 是 Q1-2 为 DEBT-016 临时加的，DEBT-016 清掉后要去掉）。
- **文件 500 行软上限**（`scripts/check_line_limits.py` CI 硬门禁，含 test/）。写完 `wc -l`。
- **DEBT-017 优先**：ring_buffer OOB 是 production 真 bug，建议 Q2 前或 Q4 前优先修（修后 ci.yml host-tests flip `-DCINUX_HOST_ASAN=ON`，Q1-5 留的开关）。

---

## 必读文件清单（接手按需读）

| 文件 | 看什么 |
|------|--------|
| `document/ai/PLAN.md`「🔄 F-QA」段 | 里程碑骨架 + Q1 ✅ 批表 + 执行序（**权威**） |
| `document/todo/quality/debt.md` | DEBT-001..017 全表（Q2 改 audit-guide 后会加 D13/D14） |
| `document/todo/quality/audit-guide.md` | **Q2 的改造对象**（当前叙述式，要 deterministic 化） |
| `document/ai/QUALITY-GATES.md` | 每轮门禁流程（G0-G8 + R0-R5 + 验证矩阵） |
| `document/ai/prompts.md` | `/preflight` `/quality-review` `/infra-audit` `/fix-debt` 入口 |
| `document/notes/2026-06-20-f-qa-q1-*.md` | Q1 各批笔记（背景/决策/陷阱/验证） |
| `document/ai/CODING-TASTE.md` | 写代码前读（命名/注释/ErrorOr 惯用法） |

---

## 下一个 AI 的第一个动作建议

1. `/resume`（读 `document/ai/PLAN.md` + `git log`），确认 Q1 已合 main（`103903e`）、当前在 `feat/f-qa-q2`。
2. （内务）清旧分支：`git branch -D feat/f-qa` + `git push origin --delete feat/f-qa`；顺手修正过时 memory（`f4`/`finfra` 实际已合 main）+ 新建 `f-qa` memory。
3. **DEBT-017**（`ring_buffer::push_batch` global-buffer-overflow，production 真 bug）建议优先修——或按用户优先级。修后 ci.yml host-tests flip `-DCINUX_HOST_ASAN=ON`。
4. 开 **Q2**：先读 `document/todo/quality/audit-guide.md` + 本文「下一步 Q2」段，propose deterministic 改造草案等用户确认（Q2 改审计方法论属"跨切面大改"，propose-then-execute）。
