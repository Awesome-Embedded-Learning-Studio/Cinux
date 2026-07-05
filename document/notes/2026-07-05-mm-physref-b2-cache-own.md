# C 重构 批 2:page cache 用 CachePhysRef 类型化 cache own

日期:2026-07-05
分支:**feat/f-usability-b4-gpp**(未 push)
HEAD:**5d02904**(批 2)
前篇:`2026-07-05-c-refactor-physref-handoff.md`(批 1 交接)
plan:`.claude/plans/mossy-sparking-wigderson.md`

## 做了啥

批 1(99bc982)建了 `PhysRef<Tag>` 框架但没 caller。批 2 接入第一个 caller —— **page cache 的 cache-ownership ref**。

三文件改动(纯结构改造,语义等价 f06ea6b):

- **`kernel/mm/phys_ref.hpp`**:加 `PhysRef() noexcept = default;`。PhysRef 因 user-declared move / deleted-copy / destructor,implicit 默认构造被抑制 → 含 `CachePhysRef` 成员的 `CachedPage` 默认构造被删(`new CachedPage()` 编译错)。显式 defaulted 默认构造让 `phys_{0}` 生效,析构对空 handle no-op。
- **`kernel/mm/page_cache.hpp`**:`CachedPage` 加 `CachePhysRef own;` 字段;include phys_ref.hpp。**保留 `phys`/`virt` 字段作只读快照**(accessor 零改动:page_fault.cpp 4 处、read_bytes、invalidate_range、test 都不动)。
- **`kernel/mm/page_cache.cpp`** `get_page`:raw `alloc_page + pte_count_inc` → `CachePhysRef::alloc()`;失败/race 的 raw `free_page` → 靠 `~CachePhysRef`(局部 `own` 或 `page->own` 析构 dec_and_test 归 0 → free)。成功路径 `page->own = std::move(own)`。

## 关键设计:撤掉 CachePhysRef::alloc 特化(RAII 闭合)

起初给 `CachePhysRef::alloc` 写了特化(`alloc_page + pte_count_inc`,匹配 B 的 cache own=2 份)。**这是错的**:

- alloc 进 pte_count=2,但一个 PhysRef 析构只 dec 1 → `pte_count_dec_and_test` 从 2 dec 到 1,**不归 0、不 free**。
- 失败路径(read 失败/race)靠 `~CachePhysRef` 时 → 漏 free → leak。

**正解**:让 `alloc_page` 设的 `pte_count=1` **就是** cache own 那一份(primary template alloc,不特化):

| 阶段 | 模型 Y(批 2 用) | B / f06ea6b(原) |
|---|---|---|
| get_page | alloc(pte_count=**1**,cache own) | alloc(1) + inc(**2**,cache own) |
| file_fault map PTE | pte_count_inc(**2**) | pte_count_inc(**3**) |
| teardown/munmap | pte_count_dec_and_test(**1**,cache own 保) | dec_and_test(**2**,cache own 保) |
| 失败/race | `~CachePhysRef` dec 1→**0**→free ✓ | `free_page` 强制 free(无视计数) |

两者 teardown 后都 > 0(cache page 不被 PMM 复用 → lto_plugin corrupt 防住),**等价**。差异在 cache own 份数:Y=1,B=2。Y 更干净(RAII 闭合:失败靠析构自动 free,B 的 2 份在 RAII 下漏 1 份 leak)。无 cache evict(CachedPage 不主动 delete),所以批 2 无行为差异。

## 范围修正:VMA 不动(plan 那条不成立)

plan 批 2 写「VMA backing 加 `variant<CachePhysRef, AnonPhysRef>`」。**与代码不符,砍掉**(用户确认):

- `VMA` 结构只有 `Inode* backing`(`vma.hpp:104`),**不持 phys**。
- phys 是 **per-page**(fault 时 alloc / 取 cache),VMA 是 **per-range**(一个 1GB mmap 是 1 个 VMA 跨 26 万页)。VMA 持单个 PhysRef 无意义。
- file-backed phys 的 owner 是 `CachedPage`(批 2 类型化);anon/CoW phys 的 owner 是「PTE 集合」(批 3 靠 PMM 拆 refcount/pte_count + caller 双 dec,不需 VMA 持对象)。

批 2 实际范围 = **只动 page_cache**(3 文件),VMA / file_fault / anon 全留批 3。

## 验证

```
全量编译:cmake --build build -j$(nproc)        ✓(零 warning)
两 leg:timeout 120 cmake --build build --target run-kernel-test-all
        单核 1108/0 ✓ + SMP ALL TESTS PASSED ✓
host:  cmake --build build --target test_host   ✓ 100%
```

无 panic / corruption / double-free(test_as_destroy_safe 验 teardown 无 kernel corruption,test_pmm_edge / test_slab_doublefree 都过)。

**GUI gcc 端到端留批 3**(语义切换 lto_plugin corrupt 硬点);批 2 语义等价 f06ea6b,kernel test 已覆盖 page_cache 路径(test_file_mmap / test_page_cache / test_as_destroy_safe)。

## 下批(批 3)衔接

批 3 = **PMM 拆 refcount/pte_count + 语义切换**(plan 的核心,治 lto_plugin corrupt 真根治):

- PMM 加 `int16_t* refcount_storage_`;`alloc_page` 设 `refcount=1` + `pte_count=0`;`pte_count_dec` 不再 free;加 `refcount_inc`/`refcount_dec_and_test`(`free_page` private friend PhysRef)。
- `PhysRef` 切 `refcount_*`(share 用 refcount_inc,析构 refcount_dec_and_test)。
- 12 caller 拆 inc/dec(交接 note 清单:`fork.cpp` 65/91、`page_fault.cpp` 315、`sys_shm.cpp` 199 留 pte_count_inc;`page_cache.cpp` 已类型化;`address_space.cpp` 193、`process_new.cpp` 119、`execve.cpp` 126、`sys_mmap.cpp` 188、`process.cpp` 118、`sys_shm.cpp` 193/247 拆 pte_count_dec + refcount_dec_and_test)。
- **必须 GUI gcc 端到端**(两次 gcc + ./a.out,lto_plugin 不 corrupt 回归)。

接 [[mm-mapcount-munmap-cache-phys]] / [[gui-gcc-gp-tmp-create]] / [[c-refactor-physref-handoff(批1)]]。
方法论 [[dont-ask-whether-to-investigate]](撤特化靠加仪器推 pte_count 轨迹,非猜测)。
