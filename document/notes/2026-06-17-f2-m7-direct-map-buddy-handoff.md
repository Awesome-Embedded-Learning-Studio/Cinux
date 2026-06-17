# 交接：F2-M7 direct-map + buddy（未完成，需彻底修复）

> 日期 2026-06-17 · 交接文档（上下文将满，写下个 session 接手）· 态度：彻底修复，不糊
> 接手第一步：`/resume`（读 PLAN + memory）→ 本 note → checkout `feat/f2-m7-direct-map`。

## 大目标

F2-M7 = buddy 伙伴系统替换 PMM bitmap。做 buddy 时发现 **direct-map 架构缺失**（buddy 崩的根因），先做 direct-map 真修，再做 buddy wiring。**两个都还没真正收敛，有真 bug。**

## 已合入 main 的 solid 基线（不动）

- **main = M6 ext2 Cache（#12 `4834c0a`），734/0，CI 过**。这是唯一可信绿基线。所有 direct-map/buddy 实验分支都**未 push**，有 bug。

## 分支状态（都本地，都有 bug，别 push）

| 分支 | 内容 | 状态 |
|---|---|---|
| `feat/f2-m7-buddy` | buddy 批1（侵入式伙伴 + test_buddy 8 测） | 单测 742 绿，但侵入式设计需 direct-map 才能接 PMM |
| `feat/f2-m7-direct-map` | direct-map 批1+2+收尾（3 commit） | **有 reserved-bits PF bug，不可 ship**（见下） |
| `feat/f2-m7-buddy-wiring` | direct-map + merge buddy + PMM 接 buddy 尝试 | 已 `git checkout -- .` 回 merged 态；wiring 有踩踏 bug |

## Bug 1（必须先修）：direct-map reserved-bits PF

**现象**：fresh clean build（`rm -rf build` + `cmake -DCINUX_BUILD_TESTS=ON`）下，`run-kernel-test` 崩：
```
[TEST] direct-map identity probe: OK
[ERROR] Page Fault (#PF): protection violation read kernel, reserved bits
[ERROR] Faulting address (CR2) = 0xFFFF880000001880   err=0x9 (present+RSVD)
```
- CR2 = `DIRECT_MAP_BASE + 0x1880` = direct-map 镜像 phys 0x1880 = **PML4[272] 条目所在 phys**。
- 触发：VMM 读 PML4 经 `phys_to_virt(0x1000)`（pml4_phys=CR3=0x1000）→ `DIRECT_MAP_BASE+0x1000`，取 `[272]` → 访问 `DIRECT_MAP_BASE+0x1880` → 该地址的页表 walk 命中 reserved bit。
- **关键**：identity probe 只测了 `DIRECT_MAP_BASE + 0x200000/0x800000/0x1000000`（**全 ≥2MB，落 PD[1+]**），**漏测首 2MB（PD[0]）**。探针过了，真用（VMM 读 PML4 落首 2MB）就崩。→ **direct-map 的首 2MB 大页（PD[0]@phys 0x11000）有问题**，PD[1+] 正常。

**之前误报"批2 734 绿"**：当时 build dir 累积状态掩盖了此 bug（没 wipe 全新验）。教训：**direct-map 改动必须 `rm -rf build` + 全新 cmake 验**，增量 build 不可信。

**direct_map_up_to 现状**（[mini paging.hpp](../../kernel/mini/arch/x86_64/paging.hpp)）：
- PML4[272] → 新 PDPT@**phys 0x10000** → 1GB 大页（PDPE1GB）或 2MB 大页（每 1GB 一 PD 页 @ **0x11000, 0x12000…**）。
- PT 页区选 `[0x10000, 0x20000)`（注释说 boot 结构 0x1000-0x7C00 之上、mini kernel 0x20000 之下，<1MB 不过 PMM）。
- direct_map_up_to 在 [big_kernel_loader.cpp:198](../../kernel/mini/big_kernel_loader.cpp#L198)（`identity_map_up_to` 后）调用。

**最可能的根因（下个 session 首查）**：**phys 0x10000-0x19000 在 big-kernel 加载过程中被踩**。
- direct_map_up_to 写完 PD@0x11000 后，loader 继续 ELF 加载（staging buffer / 拷段）。若 staging buffer 或加载过程用了 0x10000-0x19000 低内存 → 踩烂 PD（尤其 PD[0]@0x11000）。
- `check_memory_overlaps` 只注册了 "Page Tables {0x1000, 0x4000}"，**没注册 direct-map PT 区 0x10000-0x19000**，故 overlap 检查抓不到。
- **验证**：在 direct_map_up_to 末尾 + big kernel 跳转前，dump PD[0]（phys 0x11000 经 `KERNEL_VMA+0x11000` 读）值，看是否被改成带 reserved bit（如 NX bit63，或非对齐）。加诊断：探针补测 `DIRECT_MAP_BASE+0x1000`（首 2MB）。
- **修法方向**：把 direct-map PT 页挪到**确认全程空闲**的位置（如 mini kernel 之上的更高低地址，或 loader 显式预留并注册到 overlap 检查），或 loader 加载完再建 direct-map PT。

## Bug 2：buddy wiring 踩踏（修完 Bug 1 再做）

buddy 接 PMM（`feat/f2-m7-buddy-wiring`，PMM bitmap→buddy）后：
- PMM init OK（Total 9216MB Free 8171MB——**注意 9GB vs QEMU 8GB 差 1GB，待查 E820**），test_pmm 过，跑很多测试。
- **坑 a**：free list LIFO + `mark_free_region` 低→高 head-insert → pop 返回**高 phys 优先** → DMA 拿到 8.95GB（phantom，超 8GB 实 RAM）→ 三重故障。已改 `pop_free` 取最低块（low-first，分支上未提交，回退了）。
- **坑 b**：low-first 后暴露**更深踩踏**——wm 测试 #GP（poison 0xCAFEBABEDEADC0DE 被解引用，栈金丝雀值，非根因，是踩踏症状）。
- buddy 单测（256 页假区域）全绿，**但接真实 8GB + fork/mmap/AHCI 复杂模式有微妙交互没查清**。

**buddy 设计**（[buddy.hpp](../../kernel/mm/buddy.hpp)）：侵入式 free-list（`FreeBlock* next` 写空闲页自身 direct-map），`block_order_` 字节数组（1B/页 @ `__kernel_stack_top`）记 head order，free 以记录 order 为权威（kNotAllocatedHead=0xFF sentinel 判 double-free/interior）。PMM 接入保接口（`alloc_page[_locked]`/`alloc_pages(count)`/free/count）+ 锁契约（`_locked` 不取锁，PF IF=0）。

## 下个 session 怎么做（彻底修复）

1. **先修 Bug 1（direct-map PD[0]）**：checkout `feat/f2-m7-direct-map`，`rm -rf build && cmake -B build -S . -DCINUX_BUILD_TESTS=ON`，加 PT dump 诊断定位 PD[0] 是否被踩 + 谁踩。修 PT 区位置 / 加载序。**验证门：fresh build run-kernel-test 734/0 + 探针补测首 2MB + 实机 `make run`。**
2. **再修 Bug 2（buddy wiring）**：在 direct-map 修好基础上，checkout wiring 分支，系统 debug 踩踏（加 buddy invariant 检查：每 op 后验 free_lists 一致性 + order_ 数组完整性；隔离是 order array 踩 / coalescing 错 / 还是别处）。low-first pop_free 保留（是对的方向）。
3. **规矩**：direct-map/buddy 改动**一律 fresh build 验**（增量不可信，本次教训）。每批绿才提交。push/PR 用户控制。

## 已写文档（本分支）

- `document/notes/2026-06-17-direct-map-window.md`（direct-map 设计/决策——但注意"验证 734 绿"那段不可信，实际有 Bug 1）。
- PLAN 有 direct-map 段 + GOTCHA#13（注意状态需修正：direct-map 未真正完成）。
- ROADMAP 当前焦点指向 direct-map（需修正）。

## 关键 GOTCHA（本次新增，待入 PLAN）

- **direct-map 改动必须 fresh build 验**：增量 build 状态掩盖 reserved-bits PF，误报绿灯。
- **direct-map identity probe 须覆盖首 2MB**（PD[0]）：只测 ≥2MB 漏掉首 2MB bug。
- **direct-map PT 页区（0x10000-0x19000）须全程空闲**：loader staging 可能踩；须注册到 overlap 检查或挪位。
- **`-cpu max` 仅 KVM 时设**（qemu.cmake）→ qemu64 无 PDPE1GB → direct_map_up_to 须 2MB fallback（已做）。
- **buddy free list 须 low-first**（LIFO + 低→高 push 致 high-first，碰 phantom high phys）。
- **PMM Total 9GB vs QEMU 8GB**：待查（E820 报 9GB？还是 max_addr 算错）——buddy 会管到 phantom 页。
