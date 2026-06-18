# Direct-Map 独立窗口（F2-M7 真修前置）

> 日期 2026-06-17 · direct-map 架构修复 · 分支 `feat/f2-m7-direct-map`
> 把 `phys_to_virt` 从 `KERNEL_VMA` 窗口迁到独立大窗口 `DIRECT_MAP_BASE`（loader 1GB/2MB 大页 identity 映全 RAM）——修 latent >1GB direct-map bug，并为 buddy 接 PMM 铺路。

## 背景（为什么做）

F2-M7 批2（buddy 接 PMM）崩死循环（`0x80000000` 反复 PF 映射不粘住）。诊断：**不是 buddy 逻辑错**（buddy 单测 742 绿），是 direct-map 架构缺失——

- `phys_to_virt(phys) = phys + KERNEL_VMA`（`KERNEL_VMA=0xFFFFFFFF80000000`）。但 KERNEL_VMA 在 canonical 顶端，往上**窗口硬限 2GB**（phys≥2GB 地址回绕）；boot 只 higher-half 映了前 1GB（`PDPT[510]→PD`，`PDPT[511]` 没填）。
- mini loader 的 `identity_map_up_to` 只把 **identity（virt=phys）** 扩到全 RAM；higher-half（phys_to_virt 实际用的）**只覆盖 0-1GB**。`phys_to_virt(phys>1GB)` 落未映射处 → demand-page 映到随机 phys（非 identity）。
- buddy 侵入式链表把 `FreeBlock*` 写进空闲页的 `KERNEL_VMA+phys`，遍历 8GB 时 high phys 写入触发 PF → 重入 buddy（`handle_pf→alloc_page_locked`）→ 状态踩烂 → 死循环。
- 这也是 `phys_to_virt`/page_cache/DmaPool/execve 对 high phys 的 **latent bug**（之前没炸只因关键结构都 <1GB）。

## 设计

- **`DIRECT_MAP_BASE=0xFFFF880000000000`**（PML4[272]，512GB 独立窗口，与 KMEM heap PML4[256] 分离）。
- **loader 映射**（[mini paging.hpp](../../kernel/mini/arch/x86_64/paging.hpp) `direct_map_up_to`）：`PML4[272]→新 PDPT@0x10000→identity 映 [0,maxphys)`。1GB 大页 fast-path（PDPE1GB），否则 **2MB 大页**（每 1GB 一 PD 页 @0x11000..）——**不依赖 PDPE1GB**，qemu64 默认 CPU 通用。
- **PT 页放 `[0x10000,0x20000)`**：boot 结构（0x1000-0x7C00）之上、mini kernel（0x20000）之下，且 <1MB 不过 PMM，持久。
- **centralize `phys_to_virt`**（[phys_virt.hpp](../../kernel/arch/x86_64/phys_virt.hpp)）：基址 DIRECT_MAP_BASE，删 4 处重复定义。
- **迁移 direct-map 站点**（→ DIRECT_MAP_BASE）：page_cache / dma_pool / execve（页表 walk+ELF）/ usermode（GS 页）/ process_new（CoW 拷贝）。
- **保留 kernel-image 站点 KERNEL_VMA**：pmm.cpp bitmap 放置（`__kernel_stack_top` 在 kernel image）、kernel image 链接基址、boot higher-half PT 访问。

## 关键决策

1. **独立窗口而非扩 KERNEL_VMA**：KERNEL_VMA 窗口硬限 2GB（canonical 顶端），8GB RAM 映不下；必须挪基址。DIRECT_MAP_BASE 选 `0xFFFF8800…`（Linux `page_offset_base` 风格，TB 级）。
2. **2MB 页 fallback 不依赖 PDPE1GB**：批1 初版只用 1GB 页，但 `-cpu max` 仅 KVM 时设（qemu.cmake），本环境无 /dev/kvm → qemu64 → `has_1gb_pages()` false → direct_map_up_to 早退 → PML4[272]=0 大内核探针 PF。改 2MB 页（每 1GB 一 PD）后 qemu64 通用。
3. **centralize phys_to_virt**：4 处重复定义（匿名/全局）合一到 phys_virt.hpp，改基址一处生效，删冗余本地 KERNEL_VMA 常量。
4. **严判 direct-map vs kernel-image**：`__kernel_stack_top - KERNEL_VMA`（pmm bitmap）、kernel image 链接是 kernel-image 相对（kernel image 映在 KERNEL_VMA），保留 KERNEL_VMA；访问任意 PMM 页（页表/DMA/缓存页/GS）是 direct-map，迁 DIRECT_MAP_BASE。

## 陷阱

- **`-cpu max` 仅 KVM 时设**：qemu.cmake `if(/dev/kvm) set(QEMU_ACCEL -accel kvm -cpu max)`。无 KVM（CI/WSL 无 accel）落回默认 qemu64，**不暴露 PDPE1GB**。direct-map 必须有 2MB 页 fallback，不能假设 1GB 页。
- **窗口算 PML4 索引**：`DIRECT_MAP_BASE=0xFFFF880000000000`，`>>39 & 0x1FF = 272`（不是直觉的 257；+2^43 from KMEM_BASE[256]，非 +2^39）。批1 初版踩过。
- **mini kprintf 不支持 `%lx`**：mini 是 C 版 kprintf（lib/kprintf.h），`%lx` 不解析（打印字面量）。loader 诊断用 `%p`，大内核探针用大内核 kprintf（支持 `%lx`）才看到 `pml4[272]=0x10003`。
- **PT 页区选择**：0x4000-0x4FFF 看似空闲但 2MB 页需 9 页（PDPT+8 PD）放不下且 0x5000(E820)/0x7000(BootInfo) 在内；改用 [0x10000,0x20000)（mini kernel 在 0x20000，安全）。
- **qemu 镜像锁**：`qemu_test_wrapper` respawn qemu，pkill 后仍占 `cinux_test.img` 写锁 → "Failed to get write lock"。须连 wrapper 一起 kill（`ps -eo pid,comm | grep qemu`）。

## 验证

- 批1（加法，phys_to_virt 未动）：run-kernel-test 734/0；探针 `pml4[272]=0x10003` + `direct-map identity probe: OK`（DIRECT_MAP_BASE vs KERNEL_VMA 低 phys 字节一致 = identity）。
- 批2（cutover）：run-kernel-test 734/0（数不变=纯重构）、host `test_host` 49/0、生产内核 `make run` 启动到 GUI 桌面（ext2 挂载→Desktop→Mouse→Desktop icons→PIT tick）无 panic/halt/PF（页表/DMA/execve/GS 经大窗口）。

## 遗留

- **buddy 接 PMM（F2-M7 兑现）**：direct-map 前置已就绪，buddy（feat/f2-m7-buddy 批1，742 绿）接入 PMM 后其侵入式链表写 `DIRECT_MAP_BASE+phys` 对 high phys 安全（identity），不再死循环。buddy wiring 待 buddy PR 合入后另开分支。
- **GOTCHA #7（direct-map 勿 unmap）**：原指 KERNEL_VMA direct-map；现 direct-map 是 DIRECT_MAP_BASE 窗口，"勿 unmap"语义不变（DMA pool return 只 free phys）。
