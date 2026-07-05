# C 重构 批 4:失败回滚走 refcount_dec + CoW 诊断加 refcount(收尾)

日期:2026-07-05
分支:**feat/f-usability-b4-gpp**(未 push)
HEAD:**f37049a**(批 4)
前篇:`2026-07-05-mm-physref-b3-refcount-split.md`(批 3 语义切换)
plan:`.claude/plans/mossy-sparking-wigderson.md`

## 做了啥

批 4 精简收尾(4 文件 +9/-5):

- **失败回滚 5 处** raw `free_page(_locked)` → `refcount_dec_and_test`:anon fault(page_fault)/ file_fault CoW 副本(page_fault)/ ELF PT_LOAD read+map 失败(elf_load)/ sigret map 失败(execve)。刚 alloc page refcount=1,dec 归 0 → buddy free(结果同 `free_page`,但走 ownership 计数,模型一致)。
- **fault_diag CoW 失败诊断**加 `refcount_load`(配合批 3 拆 refcount/pte_count,诊断更全)。

## 范围调整(为什么不做 plan 完整批 4)

plan 批 4 = free private + 页表/slab/elf/shm/dma 全类型化 + 删旧。**我精简了**,只做失败回滚 + 诊断。理由:

- **批 2+3 已治本用户场景**:cache page ownership 由 refcount 守住,teardown 碰不到,lto_plugin corrupt 真根治(GUI gcc + g++ runtime 验证通过)。
- **页表/slab/dma/shm/kernel-stack 是 kernel-internal page**(不参与 user pte_count),合法 raw free,类型化边际收益小;且页表类型化涉及 `address_space`/`vmm`/`fork` 核心路径,high-risk,交接 note 本就建议 fresh 会话。
- **free private**(friend PhysRef):kernel-internal 合法 free 仍需例外,friend 一多 private 意义降。治本已由批 2+3(user data page ownership 类型化)达成。

## follow-up(转出,已登记)

- **页表 `PageTablePhysRef` / slab / dma / shm / kernel-stack 类型化**:high-risk 核心路径,fresh 会话推。
- **free private**:kernel-internal 合法 free 收口后。
- **page_cache LRU evict**(批 2 grow-only follow-up)。
- **gcc 编译卡顿 perf**(memory `gcc-compile-stutter-perf`):用户 2026-07-05 报"g++ 编译卡炸了"。先加观测(free_page_count / cached_pages / #PF 计数)定位瓶颈,别盲目优化。
- **gcc/g++ 缺失 syscall**(memory `gcc-missing-syscalls`):大量 ENOSYS,先 dump top-N 再决定补 stub/实现。

## 验证

```
全量编译:cmake --build build -j$(nproc)         ✓
两 leg:timeout 120 run-kernel-test-all
        单核 1109/0 + SMP ALL PASSED(2× ALL TESTS PASSED)
host:  cmake --build build --target test_host   ✓ 100%
```

失败回滚改的是罕见 OOM/read/map-fail 路径,正常 gcc 不触发;GUI gcc runtime 仍批 3 验证状态(用户 g++ 也通)。

## C 重构(批 1-4)收官状态

| 批 | commit | 范围 | 状态 |
|---|---|---|---|
| 1 | 99bc982 | mapcount→pte_count 改名 + PhysRef<Tag> 框架(定义无 caller) | ✅ |
| 2 | 5d02904 | CachedPage 持 CachePhysRef(cache own 类型化) | ✅ |
| 3 | cf5f625 | PMM 拆 refcount/pte_count + 语义切换(lto_plugin corrupt 真根治) | ✅ |
| 4 | f37049a | 失败回滚走 refcount_dec + CoW 诊断 | ✅ |

**治本达成**:sys_munmap 那类「直接 free 绕过 refcount」整族 bug —— cache page ownership 由独立 refcount 管,teardown 碰不到,类型系统 + 组合语义双拦。g++/gcc runtime 端到端验证通过。

接 [[mm-physref-refcount-split]](批 2+3)/ [[mm-mapcount-munmap-cache-phys]](B 治标)/ [[gui-gcc-gp-tmp-create]](gcc 闭环)。
方法论 [[dont-ask-whether-to-investigate]](范围调整靠评估 kernel-internal vs user-data 边际收益,非盲从 plan)。
