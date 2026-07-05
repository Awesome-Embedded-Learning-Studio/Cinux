# C 重构 批 3:PMM 拆 refcount/pte_count + 语义切换(lto_plugin corrupt 真根治)

日期:2026-07-05
分支:**feat/f-usability-b4-gpp**(未 push)
HEAD:**cf5f625**(批 3)
前篇:`2026-07-05-mm-physref-b2-cache-own.md`(批 2 cache 类型化)
plan:`.claude/plans/mossy-sparking-wigderson.md`

## 做了啥

批 3 拆开 PMM 的单一计数器(mapcount/pte_count)成 **pte_count(纯 PTE 映射数,不 free)+ refcount(ownership,归 0 free)**,对齐 Linux `_mapcount`/`_refcount`。这是 lto_plugin corrupt 的**真根治** —— cache page 的 ownership 由独立 refcount 管,teardown 碰不到,不再靠 B 的幽灵 pte_count+1。

12 文件 +223/-122:

- **PMM**(`pmm.{hpp,cpp}`):加 `refcount_storage_`(2 bytes/page,跟 `pte_count_storage_` 同段,init page-aligned 分配);`alloc_page{,s,_locked}` 设 **refcount=1 + pte_count=0**(批 1 前是 pte_count=1);新增 `pte_count_dec`(纯)/ `refcount_inc` / `refcount_dec_and_test`(归 0 → buddy free)/ `refcount_load`;**`pte_count_dec_and_test` 改组合语义**(pte_count_dec,若归 0 则 refcount_dec_and_test,返 true = 真归 0 已 free)。
- **PhysRef**(`phys_ref.hpp`):`share()`/`drop_()` 切 `refcount_inc`/`refcount_dec_and_test`;`CachePhysRef::alloc` **不加额外 inc**(primary `alloc()`,refcount=1 即 cache own;map ownership 由 install 时 `refcount_inc` 给)。
- **install 补 `pte_count_inc`**(批 3 alloc 不再设 pte_count=1,所有 install PTE 必须显式 account):anon fault(`page_fault.cpp`)/ 用户栈(`user_launch.cpp`)/ ELF PT_LOAD(`elf_load.cpp`)/ sigret(`execve.cpp`)/ CoW new(`process_new.cpp`)。
- **cache/shm install 加 `refcount_inc`**(map ownership,防 teardown free):file_fault 共享 cache page(`page_fault.cpp`)+ shmat(`sys_shm.cpp`)。
- **teardown 4 处删 `if dec_and_test free_page` → `dec_and_test`**(组合内部自动 free,防 double-free):`address_space.cpp` free_subtree / `execve.cpp` clear_user_mappings / `sys_mmap.cpp` munmap / `process_new.cpp` CoW-old。
- **fork `pte_count_inc` 留**(65/91,共享映射计数;组合 teardown 闭合)。**sys_shm undo/shmdt 不变**(`static_cast<void> dec_and_test` 组合)。
- **test 重写**(`test_pmm_pte_count.cpp`):批 3 模型 + `test_cache_page_survives_teardown`(直接验 cache page teardown 不 free)+ `test_real_copy_page_table_level_cow` 改。

## 关键设计:组合语义(pte_count_dec_and_test 内部条件 refcount_dec)

plan/交接 note 写「teardown 拆 pte_count_dec + refcount_dec_and_test(无条件双 dec),fork 加 refcount_inc」。**我推 refcount 轨迹发现 plan 的无条件双 dec 对 fork CoW 是 bug**:fork 后 refcount=1(alloc),父 teardown 无条件 refcount_dec → 0 → free,子还映射 → UAF。

**正解:组合语义** —— `pte_count_dec_and_test` 改成「pte_count_dec,若 pte_count 归 0 则 refcount_dec_and_test」。caller 零拆(`if dec_and_test free_page` → `dec_and_test`),且 fork **不需加 refcount_inc**(refcount 由 pte_count 归 0 条件 dec,自动等 fork 共享 page 最后一个 unmapper)。

### 模型全闭合(推过的 5 条轨迹)

| 场景 | refcount 轨迹 | 闭合 |
|---|---|---|
| **anon**(brk/MAP_ANON/stack) | alloc=1 → install pte_count=1 → teardown dec_and_test(pte_count 0→ refcount 1→0 free) | ✓ |
| **fork CoW 共享** | alloc=1, pte_count=1 → fork pte_count_inc(=2) → 父 dec_and_test(pte_count 1,不 refcount_dec)→ 子 dec_and_test(pte_count 0→refcount 1→0 free) | ✓ |
| **cache page**(单进程) | CachePhysRef::alloc=1 → install refcount_inc=2 + pte_count=1 → teardown dec_and_test(pte_count 0→refcount 2→1,cache own 保)→ evict ~CachePhysRef refcount_dec(1→0 free) | ✓ |
| **cache page + 多进程** | refcount=1(cache)+ N(map) → 每 teardown dec_and_test(pte_count>0 不动 refcount;最后一个 pte_count 0→refcount dec map own)→ evict dec cache own | ✓ |
| **cache race**(get_page 竞态,从未 install) | CachePhysRef::alloc=1 → delete page → ~CachePhysRef refcount_dec(1→0 free) | ✓ |

**CoW fault**(写共享):旧页 dec_and_test(pte_count-1,若归 0 → refcount_dec) + 新页 alloc + install(pte_count_inc,不 refcount_inc —— alloc=1 就是 anon own)。两条独立闭合。

### 模型差异:anon install 不加 refcount_inc,cache install 加

anon page 的 alloc refcount=1 就是「映射它的地址空间 own」(install 不再 inc);cache page 有长期 owner(CachedPage),install 加 refcount_inc 表达「这一份 map own」,teardown dec 它,cache own(alloc=1)保活。这正是 Linux anon install 不 `get_page`、file install `get_page` 的差异。

## 范围修正(对 plan)

- **fork 不加 refcount_inc**(plan 暗示双 inc):组合语义下 fork 只 pte_count_inc,refcount 由 pte_count 归 0 触发 dec。caller 改动更小(fork 65/91 零改)。
- **pte_count_dec_and_test 保留**(plan 说删):改组合语义而非删,7 处 teardown caller 机械改(删 `if + free_page`)。test 重写覆盖。
- **sys_shm 不改 shmctl free_pages**(direct buddy,留批 4 类型化);undo/shmdt 的 `static_cast<void> dec_and_test` 组合语义正确(撤 map own,segment own 保),不改。

## 验证

```
全量编译:cmake --build build -j$(nproc)         ✓(零新 warning)
两 leg:timeout 120 run-kernel-test-all
        单核 1109/0(+1 新测试)+ SMP ALL PASSED(2× ALL TESTS PASSED)
host:  cmake --build build --target test_host   ✓ 100%
装配:  assemble-gcc-rootfs                      ✓(201MB rootfs-gcc.ext2)
```

新增 `test_cache_page_survives_teardown` **直接验 lto_plugin corrupt 保证**(cache page teardown 后 refcount>0 不 free)。`test_real_copy_page_table_level_cow` 验 fork 真路径(pte_count=2 父子共享)。无 panic / double-free / UAF(grep 命中均为测试名 `test_double_free_noop` 等)。

**GUI gcc runtime 端到端需用户手动 VNC**(交接 note 步骤:`run-single` → `gcc -fno-pie -no-pie hello.c && ./a.out` ×2 → debugfs 查 lto_plugin/crt1.o 仍 valid)。批 3 的核心保证(cache page survives teardown)已由 kernel test 直接覆盖,但 lto_plugin runtime 是端到端确认,留给用户。

## 下批(批 4)衔接

批 4 = **free private + 页表/elf/stack/shm/dma 类型化 + 删旧**(plan 收官):

- `free_page`/`free_page_locked` **private**(`friend PhysRef<Tag>`)+ 批 3 遗留的直接 `free_page` caller 改 PhysRef 或组合 API:page_fault 失败回滚(anon/cow_phys)、sys_shm shmctl free_pages、页表页(address_space/vmm/fork/clone/ap_main 的 intermediate PT)、slab grow/析构、elf_load PT_LOAD 失败回滚、dma_pool。
- 页表 `PageTablePhysRef`(PML4/PDPT/PD/PT)。
- fault_diag 加 `refcount_load` 诊断(批 3 留)。
- 删 raw `alloc_page`/`free_page` public(全迁移完)+ 旧 `pte_count` 残留。

**风险点**:free private 后,任何直接 `free_page(phys)` 编译错 —— 这就是 sys_munmap 那类 bug 的类型层根治。批 4 改动面广(~10 文件 direct free_page caller),分 fresh 会话推。

接 [[mm-mapcount-munmap-cache-phys]](B 治标)/ [[mm-physref-b2-cache-own]](批 2)/ [[gui-gcc-gp-tmp-create]](gcc 闭环)。
方法论 [[dont-ask-whether-to-investigate]](组合语义靠推 refcount 轨迹实证,非盲从 plan)。
