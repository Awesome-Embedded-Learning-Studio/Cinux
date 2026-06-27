# CinuxOS — 测试覆盖矩阵（Test Coverage Matrix）

> **F-VERIFY M1 交付物（骨架）。** 平行 [debt.md](debt.md)（代码债）的另一根轴：debt.md 量「代码写得对不对」，本表量「**代码有没有真被测到**」。
>
> **起源**：2026-06-27 audit 抽 165 调试时间坑——19 个根因是「基建≠生产」（测试路径跟崩的那条根本不是一条）、27 个是「机制没真生效」（绿了≠位真置上了）。本表让这两个盲区**可见、可追踪**。
>
> **状态机**：每格给状态 + grep/test 取证。🆕 待填 → ✅ 覆盖 / 🟡 部分 / ⚠️ 假测（镜像副本/哨兵/够不到真路径）/ ❌ 缺。
> **流程**：M1 用 `grep`/读码逐格坐实；F-VERIFY 各批补的测试回头把 ⚠️/❌ 升 ✅；新功能签收必须填一行（与 debt.md 纪律一致）。

## 图例

| 标记 | 含义 |
|------|------|
| ✅ | 真覆盖：跑的是真代码/真路径/真回读，断言有效 |
| 🟡 | 部分覆盖：覆盖了但维度不全（如只 BSP 没 AP、只单核没 -smp） |
| ⚠️ | **假测**：看着绿但不证明任何事（镜像副本、哨兵指针、够不到 ring3/CoW 真路径） |
| ❌ | 缺：没有任何测试触达 |
| — | 不适用（该子系统在该维度无意义） |

## 维度（列）

| 列 | 含义 | 例子 |
|----|------|------|
| **host-unit** | `test/unit/*.cpp` 纯算术/mock，g++ 编，不链接内核码 | test_pmm 算术 |
| **host-integration(真码)** | host 测试**链接真内核 .cpp**（非镜像副本），配 MockPMM/mock AS | 当前仅 ~12 个真 .cpp 被链接 |
| **QEMU-kernel** | `run-kernel-test`，big_kernel_test，ring0、IF=0、BSP-only | 954/0 主力 |
| **QEMU-SMP** | `run-kernel-test-smp`——⚠️ 当前空转（不 boot_aps），M3 后才真 | 名存实亡 |
| **ring-3 userspace** | 真用户任务（有 AddressSpace）经 syscall/execve 跑 | musl smoke（opt-off）|
| **机制回读** | 使能位/MSR/CR 读回断言真置上（非「没 #GP 就算过」）| test_f9 读 CR4 |

## 矩阵（种子行 —— M1 逐格 grep 坐实 + 补齐）

> 下表是 audit 已核实的高确定度种子，⚠️/❌ 即 F-VERIFY 各批要消的盲区。M1 扩全子系统、每格给 file:line 取证。

| 子系统 | host-unit | host-integration(真码) | QEMU-kernel | QEMU-SMP | ring-3 | 机制回读 | 备注 / F-VERIFY 批 |
|--------|-----------|------------------------|-------------|----------|--------|----------|--------------------|
| PMM/buddy/slab | ✅ test_pmm | 🟡 | ✅ | ❌(空转) | — | ❌ | — |
| VMM / 页表遍历 | ✅ test_vmm | 🟡 | ✅ | ❌ | — | ❌ | VMM::translate 不走 huge 页（FO GOTCHA） |
| AddressSpace / VMA | ✅ test_address_space | 🟡 | ✅ | ❌ | — | ❌ | — |
| **fork / clone** | — | ❌(不链接真码) | 🟡 | ❌ | ⚠️(/hello hack 非原生) | ❌ | **M2 链真码 / M5 原生压力** |
| **CoW / mapcount** | ❌ | ❌ | ⚠️(**哨兵 0x1234** test_clone.cpp:142) | ❌ | ⚠️(/hello hack) | ❌(无 mapcount 回读) | **M2 消哨兵 / M5 真压力 / M6 回读** |
| execve / ELF loader | ✅ test_elf_loader | ❌(不链接 execve.cpp) | 🟡 | ❌ | 🟡(smoke opt-off) | ❌ | **M2 链真码 / M5 继承 AS execve** |
| scheduler / 迁移 | ✅ test_scheduler | ❌(不链真码) | 🟡 | ❌(空转) | — | ❌ | **M3 SMP 真跑 / M4 TSan 迁移竞态** |
| signal / futex / waitpid | ✅ | 🟡 | ✅ | ❌ | 🟡 | ❌ | 阻塞协议单核串行永不中 |
| **syscall ABI (asm)** | ⚠️(**镜像** syscall_dispatch) | ❌(不汇编 syscall.S) | 🟡 | ❌ | 🟡 | ❌(frame size 无 static_assert) | **M2 host asm smoke / M2 static_assert** |
| **AP bring-up / MSR** | — | — | ❌ | ❌(不 boot_aps) | — | ❌(**AP LSTAR/EFER/STAR 零回读**) | **M3 boot_aps / M4 per-cpu 回读槽** |
| IRQ / 中断管道 | ✅ test_pic/pit | — | 🟡 | ❌ | — | ❌ | **M6 ISR 位清不变量** |
| SMEP / SMAP / NXE | — | — | ✅ | ❌(单核) | — | 🟡(**仅 BSP** test_f9；AP ❌) | **M4 AP 回读 + OSFXSR(M0)** |
| xHCI / USB | ✅ test_xhci | 🟡 | ✅ | ❌ | — | ❌ | DEBT-021 poll_events 并发 open |
| e1000 / net | ✅ test_net_* | 🟡 | ✅(LAPIC timer hack) | ❌ | — | ❌ | 测试内核关中断→main loop（GOTCHA） |
| ext2 / VFS / page cache | ✅ test_ext2_* | 🟡(MockFileOps 非真驱动) | ✅ | ❌ | — | ❌ | — |
| pipe | ✅ test_pipe | 🟡 | ✅ | ❌ | — | ❌ | — |
| frame/backtrace/panic | ✅ test_backtrace | — | ✅ | ❌ | — | ❌ | **M6 故障可观测增强** |

## 机制回读索引（CR/MSR/EFER 位 → 回读测试 → 目标 CPU）

> M4 交付。audit 发现仅 `test_usermode::test_f9` 一处真回读（BSP 单核）。每行使能位必须有回读测试 + 目标 CPU（含 SMP 列）；新功能使能**签收必填一行**（写进 DIRECTIVES）。

| 位 | 使能位置 | 回读测试 | BSP | AP | 备注 |
|----|----------|----------|-----|-----|------|
| CR4.SMEP | paging.cpp enable_smep_smap | test_f9 | ✅ | ❌ | M4 补 AP |
| CR4.SMAP | paging.cpp enable_smep_smap | test_f9 | ✅ | ❌ | M4 补 AP |
| CR4.OSFXSR | boot.S / ap_trampoline | — | ❌(M0 补) | ❌ | M0 扩 test_f9 |
| CR4.OSXMMEXCPT | boot.S / ap_trampoline | — | ❌(M0 补) | ❌ | M0 扩 test_f9 |
| EFER.SCE / NXE | usermode.S / ap_main | test_f9(SMEP 侧证) | 🟡 | ❌ | M4 补 AP |
| LSTAR (0xC0000082) | syscall_init(BSP only!) | — | ✅(隐式) | **❌(零回读→LSTAR==0 #DF)** | **M3/M4 头号** |
| STAR / SFMASK | usermode_init | — | 🟡(只证无#GP) | ❌ | M4 补回读 |
| stack canary | boot.S seed | — | ❌ | ❌ | M4 补回读+负测试 |

## 假测清单（⚠️，F-VERIFY 优先消除）

> 「绿但不证明任何事」的测试。每条给替代方案 + 责任批。

| 假测 | 证据 | 为何假 | 消除方案 | 批 |
|------|------|--------|----------|----|
| test_clone CoW | test_clone.cpp:142 `0x1234` 哨兵 | 不建带页表真 AS，CoW 页表拷贝路径不跑 | 链真码 + 真 AS fork | M2/M5 |
| test_syscall dispatch | 自称「Mirror of syscall_dispatch」 | 不汇编/执行真 syscall.S，ABI 漂移看不见 | host asm smoke + static_assert frame size | M2 |
| run-kernel-test-smp | main_test.cpp 不 boot_aps | -smp 2 起来但套件 BSP-only，SMP 门空转 | 测试内核镜像 boot_aps | M3 |
| musl smoke | 装 /hello 绕过 harness | opt-off、临装全新 AS 不走继承 CoW | harness 原生真用户 fork + 默认 ON | M5 |
| 各 enable_X | 返回即算过 | 不读寄存器回读，位没真置上也绿 | 机制回读矩阵 | M4 |

---

**M1 任务**：逐格 `grep`/读码坐实状态 + file:line 取证；扩全子系统（本表种子约 17 行，目标覆盖所有 kernel/ 子目录）；把 ⚠️/❌ 汇总成 F-VERIFY 各批的「待消除盲区」清单。完成后本表成为 F-VERIFY 的追踪表（同 debt.md 之于 F-QA/F-CLN）。
