# F-VERIFY M1 — 测试覆盖矩阵盘点（收官 2026-06-27）

> 横切里程碑 F-VERIFY 第二批。分支 `feat/f-verify`。M0 见 `2026-06-27-f-verify-m0-quick-wins.md`。
> 交付物：[test-matrix.md](../todo/quality/test-matrix.md) —— 平行 [debt.md](../todo/quality/debt.md)（代码债）的另一根轴：量「**代码有没有真被测到**」。

## 目标

在往里加一堆新 SMP/并发测试（M3-M5）之前，先**摸清现在到底覆盖了什么**——audit 揪出 19 个「基建≠生产」、27 个「机制没真生效」时间坑，根因就是没人有这张全局视图。M1 把它建出来，让盲区可见、可追踪。

## 打法

6-agent workflow 并行审计 6 个子系统簇（MM / proc / arch / drivers / fs-lib / sec-user），每簇逐子系统 × 6 维度 grep 坐实。6 agent、356k tokens、159 次工具调用。回收结构化数据后主循环合并成矩阵。

6 维度：host-unit（纯/mock）／host-integration（链接真码）／QEMU-kernel（ring0/BSP）／QEMU-SMP ／ring-3 用户态 ／机制回读。

## 交付：47 子系统 × 6 维度 = 282 格

矩阵见 [test-matrix.md](../todo/quality/test-matrix.md)。覆盖分布总览（damning 量化）：

| 维度 | ✅ | 🟡 | ⚠️假 | ❌缺 |
|------|----|----|------|------|
| host-unit（纯/mock） | 2 | 3 | 16 | 18 |
| host-integration（链接真码） | 5 | 3 | 2 | **37** |
| QEMU-kernel（ring0/BSP） | 23 | 18 | 1 | 5 |
| QEMU-SMP | 0 | 0 | 0 | **47** |
| ring-3 用户态 | 1 | 14 | 2 | 22 |
| 机制回读 | 1 | 12 | 0 | 18 |

另登记 **36 假测**（⚠️）+ **38 机制位**（CR/MSR/EFER → 回读测试 → BSP/AP）。

## 三大结构性盲区（量化坐实，驱动 M2-M6）

1. **host-integration 真码链接 37/47 ❌** —— 大量 host 测试是**镜像副本**（test_pmm/vmm/address_space/scheduler 自己重实现逻辑，改真码不跟着测）。更阴的是 `add_cinux_integration_test(vmm ...)` **只链 `unit/test_vmm.cpp`、不链真 `kernel/mm/vmm.cpp`**——16 个 integration 中部分「名不副实」，16 这个数夸大了真码覆盖。→ **M2** 消镜像、链真码。
2. **QEMU-SMP 47/47 ❌（空转）** —— run-kernel-test-smp 不 boot_aps，SMP 门名存实亡；所有 SMP-only bug（迁移竞态、CoW 跨核 UAF、LSTAR-AP）CI 永远抓不到。→ **M3** 真唤醒 AP。
3. **机制回读 ~27/47 ❌ + AP 侧零回读** —— 使能位没读回断言（仅 `test_usermode::test_f9` 一处读 BSP CR4 SMEP/SMAP/OSFXSR/OSXMMEXCPT）；AP 侧 CR4/MSR/LSTAR/STAR/SFMASK 全无回读，是 SMEP/SMAP 4 批假绿 + LSTAR==0 #DF 的同根。→ **M4** 回读矩阵 + AP per-cpu 结果槽。

## 抽查验证（不 ship 没核实的 agent 断言）

矩阵要长期当追踪表用，load-bearing 断言逐条核：
- **test_pmm 镜像**：[test/unit/test_pmm.cpp:9-14](../../test/unit/test_pmm.cpp#L9) 注释自承「All kernel dependencies are mocked or reimplemented」。✅
- **vmm integration 名不副实**：[test/CMakeLists.txt:186-188](../../test/CMakeLists.txt#L186) `add_cinux_integration_test(vmm unit/test_vmm.cpp)` —— 只链 unit，不链 vmm.cpp。✅
- **fork_exec 链码**：`add_cinux_integration_test(fork_exec ...)` 只链 pid.cpp + elf_types.cpp + test，CoW 真码（fork.cpp/clone.cpp/process_new.cpp）不链。✅
- **integration vs pure 计数**：16 个 integration_test vs 46 个 add_cinux_test（纯/mock）。✅
- **0x1234 哨兵**：M0 已亲验 [test_clone.cpp:142](../../kernel/test/test_clone.cpp#L142)。✅

## 验证

docs-only 批（矩阵是审计产物，无代码改动）。`cmake --build build` 仍绿（未碰代码）。

## 教训 / GOTCHA

- **「integration test」≠「链接真码」**：CinuxOS 的 `add_cinux_integration_test` 只是「允许列多个源文件」，但很多调用方只列了 `unit/test_X.cpp` 一个文件（镜像副本），没链真 `kernel/X.cpp`。命名误导——看名字以为链真码，实则还是镜像。M2 要么链真码，要么改名/标注。
- **覆盖分布比单点结论更有力**：「37/47 真码没链接」「47/47 SMP 空转」这种量化比「测试不够」具体得多，直接驱动后续批优先级。
- **宏观 audit 可信 + 逐条核实**：与 M0 一致——workflow 的量化归纳（282 格、36 假测）可信且高价值，但每条 load-bearing 断言仍要抽检（本次 5 条全过）。

## 下一步

M1 收官。F-VERIFY 后续按矩阵盲区驱动：
- **M2 测试代码整理**：消 36 假测里的镜像副本（链真码：pmm/vmm/address_space/scheduler/execve/fork·clone CoW）+ 共享 util（CurrentTaskGuard/MockAS/MockPMM）+ 拆 flat main_test。
- **M3 SMP 测试唤醒基建**（技术最硬 enabler）：解 47/47 SMP 空转。
- **M4 机制回读矩阵 + AP per-cpu 槽**：解 27/47 + AP 零回读。
- **M5 真用户 fork/CoW 压力回归**（headline）。
- **M6 故障可观测增强**。

push/PR 归用户。当前 feat/f-verify 待 push：M0（5 commit）+ M1（本批）。
