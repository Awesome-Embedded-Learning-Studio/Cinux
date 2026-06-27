# F-VERIFY M0 — 零风险快速赢点（收官 2026-06-27）

> 横切里程碑 F-VERIFY（动态验证与并发检测基建）的打地基批。分支 `feat/f-verify`。
> 起源见 audit memory `debugging-audit-dynamic-coverage-gap` + PLAN「🔄 F-VERIFY」段。
> 本批精神：**改前先坐实，不盲信 audit / 不盲信绿灯**——这条本身就是 F-VERIFY 要建立的纪律。

## 目标

落地一批零风险（不碰热路径、不改语义）的测试/基建加固赢点，给后续 M1-M6 重锤打底；同时在执行中**核实 audit 的每条具体建议**——发现夸大/误判就砍，不转述没坐实的东西。

## 落地（4 项，3 commit）

| 项 | 改动 | commit |
|----|------|--------|
| **M0-1** | 修 `RUN_ALL_TESTS` 虚报 PASS：原逐项无条件 `[PASS]`+`_tests_passed++`，即使该用例 ASSERT 失败（失败计数也 +1，PASS 计数虚高、逐项 PASS 行不可信）。改跑前快照 `_tests_failed`，仅未变才 `[PASS]`+计数；退出码契约不变。 | `4720138` |
| **M0-3** | `test_f9` 扩 `CR4.OSFXSR`(bit9)+`OSXMMEXCPT`(bit10) 无条件回读断言。二者由 `boot.S`(BSP `(1<<9)|(1<<10)`)+`ap_trampoline.S`(AP `0x620`) 无条件置位，非 CPUID 门控。读回断言使「绿但位没真置」回归 loud（SMEP/SMAP 曾 4 批假绿同类）。 | `4720138` |
| **M0-4** | 全树清扫 22 处 `0x%p`→`%p`：big+mini 两套 kprintf 的 `%p` **均自带 `0x` 前缀**（`vkprintf_impl.hpp:342` / mini `vkprintf_impl.h:200`），故 `0x%p` 输出 `0x0x...`。10 文件（task_builder/slab/ramdisk/ahci/mini elf_loader·main·big_kernel_loader·test）。`QUALITY-GATES` G7 补串口日志读法（`grep -a` + `%p` 自带 `0x`）。 | `a900a7f` |
| **M0-5** | `check_memory_layout.py` 接进 CI（host-tests job Build 之后）：脚本早已写好（解析 linker 脚本+ELF PT_LOAD+磁盘 LBA，重叠 exit 1）但从没在 CI 跑——MSI-X/xHCI BAR0 碰撞那种是 debug 会话而非 build 失败。本地验 exit 0。 | `73944b4` |

## 砍掉 / 重构（3 项，全部核实后砍——F-VERIFY 纪律的体现）

| 项 | audit 建议 | 核实结果 | 处置 |
|----|-----------|----------|------|
| ~~kMaxCpus static_assert~~ | 「两处 `kMaxCpus` 值不一致，绑 static_assert」 | 实为两**概念**：`kMaxCpus=8`(percpu)/`kMaxAcpiLapics=16`(acpi)，且 **static_assert 早就存在** [ap_main.cpp:35](../../kernel/arch/x86_64/ap_main.cpp#L35) | 砍 |
| ~~check_test_count grep -a~~ | 「`check_test_count.sh` 默认 `grep -a`」 | 脚本**早已用 `grep -a`**（GOTCHA#2 注释齐全） | 降级为 G7 手动命令约定（手动 debug 命令仍 re-offend） |
| ~~M0-2 unsigned-overflow UBSAN~~ | 「`-fsanitize=unsigned-integer-overflow` 进 CINUX_UBSAN，桩已存在」 | 桩（`__ubsan_handle_*_overflow`）确存在，**但 GCC 编译器不认这个 flag**（`unrecognized argument; did you mean signed-integer-overflow`）——它是 **Clang-only**。GCC 认为 unsigned wrap 良定义不归 UBSAN。本项目 CI+toolchain 全 GCC。 | 砍；unsigned-wrap 防护改走 execve 显式 checked 运算（移入 DEBT-020/012 债修批，非零风险）。debt.md DEBT-020 已注。 |

> 三条砍掉的都是 audit 基于「Clang/Generic 假设」或「没读最新代码」的误判。**核实→砍** 比「盲信→落地一个破 flag 让 build 红」好——这正是 F-VERIFY 要根治的「假绿/盲信」文化。

## 验证

- `run-kernel-test`（default build）**954/0**（M0-1 计数修对、M0-3 OSFXSR 回读通过）。
- `test_host` **60/0**（框架头被 host+kernel 共用）。
- `CINUX_UBSAN=ON` build-ubsan `run-kernel-test` **954/0 零 UB 命中**（撤 M0-2 flag 后顺带确认现有 UBSAN 门健康）。
- `check_memory_layout.py` exit 0（接 CI 前本地验）。
- 全量 `cmake --build build` 绿、零新 warning（`0x%p` 清扫是格式串内改动，不触发 `format(printf)` 新警告）。

## 教训 / GOTCHA

- **M0-2 GCC vs Clang UBSAN 差异**：`-fsanitize=unsigned-integer-overflow` 是 Clang 专属；GCC 只到 `signed-integer-overflow`（已含于 `undefined`）。GCC 工具链想抓 unsigned wrap 只能显式 checked 运算，无 sanitizer 兜底。audit/PLAN 凡涉及 sanitizer flag 必须先确认工具链支持。
- **`0x%p` 是双前缀**：两套 kprintf 的 `%p` 都自带 `0x`。新代码别写 `0x%p`（G7 已记）。
- **audit 会夸大**：165 坑的根因分布和家族归纳是对的（高价值），但**逐条 quick-win 建议要逐条核实**——本批 3/7 的具体建议是误判。宏观结论可信，微观行动必验。

## 下一步

M0 收官。F-VERIFY 后续（见 PLAN 批表）：
- **M1** 测试矩阵盘点（填 `test-matrix.md` 17 行种子→全子系统 grep 坐实）。
- **M2** 测试代码整理 + 共享 util（CurrentTaskGuard/MockAS、消 0x1234 假 CoW、镜像副本改链接真码）。
- **M3** SMP 测试唤醒基建（技术最硬，enabler）。
- **M5** 真用户 fork/CoW 压力回归 + 继承 AS execve（headline，防 F10 四修回归）。
- **M4** 并发检测（host TSAN + 内核 KCSAN-lite）/ **M6** 故障可观测增强（独立收口）。

push/PR 归用户。当前 feat/f-verify 待 push：`63b68cb` 立项 + `4720138` M0-1/3 + `a900a7f` M0-4 + `73944b4` M0-5 + 本批收官 commit。
