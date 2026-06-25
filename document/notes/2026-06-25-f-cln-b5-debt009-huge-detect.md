# F-CLN 批5 — DEBT-009 huge entry 检测 — 2026-06-25

> DEBT-009（clear_user_mappings/free_subtree 不识 huge page entry，误当 PT free garbage）闭环。

## 问题
[clear_user_mappings](../../kernel/proc/execve.cpp#L62) + [free_subtree](../../kernel/mm/address_space.cpp#L170) 4 层遍历**不检查 huge/PS bit**。若用户空间引入 huge 页（2MB/1GB，mmap/brk 未来），huge entry 基址（数据页）被当 PDPT/PD 表页下钻——解析 huge 内容当 PT + free garbage 物理页 → PMM 状态错乱。

## 修复（防御性，三处 huge 检测）
当前 NXE off 无 user huge 映射，修复是防御（未来 huge 不踩坑）：
- **clear_user_mappings**：PDPT[j] 层判 1GB huge + PD[k] 层判 2MB huge → kprintf warn + 清零 entry + continue（不下钻 PD/PT，不 free 数据页）
- **free_subtree**：递归各层判 huge → kprintf warn + continue（不下钻，不 free）
- **huge free（buddy order 2MB/1GB）未实现**——遇 huge 说明未来 huge 支持里程碑漏更新此路径（检测即保护）

PageEntry 有 `huge : 1` 位域（PS bit），直接 `entry.huge` 访问。

## 验证
- big_kernel 编译零 error/warning
- run-kernel-test **931/0**（无 huge，回归——检测分支不触发）
- 诚实：当前无 huge，检测是防御；真 huge free（order + mapcount）留 huge 支持里程碑

## 关联
- GOTCHA#13（direct-map huge split 破坏全局 direct-map，相关但针对 VMM.map 的 walk_level split）
- 本批针对用户空间页表遍历的 huge 识别，不碰 direct-map

## 产出
- execve.cpp（clear_user_mappings PDPT 1GB + PD 2MB huge 检测）+ address_space.cpp（free_subtree huge 检测）+ debt.md DEBT-009 ✅ + PLAN 批5 ✅

下个：批6 DEBT-010 FDTable refcount 对齐 R3。
