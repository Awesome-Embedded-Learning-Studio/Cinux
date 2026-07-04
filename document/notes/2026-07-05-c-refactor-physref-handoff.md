# C 重构(PhysRef / pte_count 分离)交接 — 批 1 完成,批 2-4 待续

日期:2026-07-05
分支:**feat/f-usability-b4-gpp**(未 push)
HEAD:**99bc982**(批 1)
前篇:`2026-07-05-mm-munmap-cache-page-phys.md`(B 修 f06ea6b)
plan:**`.claude/plans/mossy-sparking-wigderson.md`**(已批,完整设计 + 批次)

## 给下个 AI:从哪接上

**C 重构目标**:分离 `pte_count`(纯 user PTE,不决定 free)+ `refcount`(ownership,决定 free)+ `PhysRef<Tag>` RAII 句柄(move-only,`free_page` 私有)。对齐 Linux `page->_refcount`/`_mapcount`。**根治 sys_munmap 那类"直接 free 绕过 refcount"整族 bug**(类型系统拦,编译错)。

**本分支三 commit(未 push)**:
- `c6abfff` — ext2 inode cache 重设计(独立堆+链地址+refcount 回收,治 crt1.o 别名 UAF)。
- `f06ea6b` — mm sys_munmap `dec_and_test` + page cache own ref(治 lto_plugin corrupt,B 修)。
- `99bc982` — **批 1** mapcount → pte_count 改名(全 kernel)+ `phys_ref.hpp`(PhysRef<Tag> 框架)。

**批 1 做了啥(已完成,绿)**:
- sed 改名 `mapcount` → `pte_count`(全 kernel ~18 文件:PMM + 13 caller + test)。
- `git mv kernel/test/test_pmm_mapcount.cpp test_pmm_pte_count.cpp`(CMakeLists 同步)。
- 新增 `kernel/mm/phys_ref.hpp`:`PhysRef<Tag>` 模板(move-only/share/~dec)+ alias(`CachePhysRef`/`AnonPhysRef`/`PageTablePhysRef`)+ 3 Tag。**批 1 仅定义,走 public `pte_count_*`(过渡),caller 没接入**。
- 验证:run-kernel-test-all 两 leg PASS(单核 1108/0)。

**语义未变**(批 1 纯改名 + 框架):`pte_count_dec_and_test` 仍归 0 free(同旧 mapcount)。PhysRef 定义但没 caller `#include`。**批 3 才拆语义**。

## 批 2-4 任务(详细)

### 批 2:cache + VMA 持 PhysRef

**目标**:page cache + VMA backing 用 `CachePhysRef` 替代 raw phys + B 的 `pte_count_inc` own ref。

改动:
- `kernel/mm/page_cache.hpp`:`CachedPage` 加 `CachePhysRef own` 字段(替代 raw `phys`;phys 通过 `own.phys()` escape)。
- `kernel/mm/page_cache.cpp` get_page:`CachePhysRef::alloc()` 替代 `alloc_page + pte_count_inc`(B 的 cache own)。read 用 `own.phys()`。失败 `~CachePhysRef`(替代 `free_page`)。race 路径(line 129)同。
- `kernel/mm/vma.hpp`:VMA backing 加 `variant<CachePhysRef, AnonPhysRef>`(file-backed vs anon);`Inode* backing` 保留(file fault 仍需)。
- `kernel/mm/vma.cpp`:`release_backing` 析构 PhysRef + `inode_unref`。
- `kernel/arch/x86_64/page_fault.cpp` file_fault:VMA backing `CachePhysRef::share()` 映射 + `pte_count_inc`(PTE)。CoW(MAP_PRIVATE writable,line 278-298)copy 到 `AnonPhysRef`。

**陷阱**:
- `CachedPage` 含 `CachePhysRef`(move-only)→ `CachedPage` 不可 copy。它由 `new` 分配、链表节点地址稳定,OK;但 `get_page` 内部 race 路径 `delete page` 时 `~CachePhysRef` 会 free phys,确认 race 逻辑(`free_page(phys)` line 129)对应改 `~CachePhysRef`。
- 批 2 `CachePhysRef` 走 `pte_count_*`(过渡,批 3 切 `refcount_*`)。

验证:`run-kernel-test-all` + `test_host` + mmap/file-fault 单测。

### 批 3(核心):PMM 拆 refcount/pte_count + 语义切换

**目标**:`pte_count` 纯 user PTE(**不 free**),`refcount` ownership(决定 free)。`PhysRef` 切 `refcount_*`。**这是 lto_plugin corrupt 的真根治**(不再依赖 B 的 pte_count own ref hack)。

改动 PMM(`kernel/mm/pmm.{hpp,cpp}`):
- 加 `int16_t* refcount_storage_`(新,`init` 分配,2 bytes/page,跟 `pte_count_storage_` 同段);`pte_count_storage_` 批 1 已改名。
- `alloc_page_locked`:设 **`refcount=1` + `pte_count=0`**(分离;批 1 前是 `pte_count=1`)。`alloc_pages` 同(每页 refcount=1/pte_count=0)。
- `pte_count_inc`/`pte_count_dec`(批 1 已改名):**`pte_count_dec` 不再 free**(纯 PTE -1)。
- **删 `pte_count_dec_and_test`**(批 1 改名,语义混合)→ 拆:caller 用 `pte_count_dec`(纯)+ `refcount_dec_and_test`(归 0 free)。
- 加 public `refcount_inc`/`refcount_dec_and_test`(归 0 → `free_page_locked` private)。加 `refcount_load`/`pte_count_load`(诊断)。
- `free_page`/`free_page_locked` **private**(`friend template <typename Tag> class PhysRef;`)。**caller 不能直接 free(编译错)—— 这就是治本**。

改动 `PhysRef`(`kernel/mm/phys_ref.hpp`):
- `share()`:`g_pmm.pte_count_inc` → **`g_pmm.refcount_inc`**。
- `~PhysRef`/`drop_`:`g_pmm.pte_count_dec_and_test` → **`g_pmm.refcount_dec_and_test`**(归 0 free private friend)。
- `alloc()` 不变(alloc_page 设 refcount=1)。

caller(12 处,Explore 钉死):
- **`pte_count_inc` 留 PTE**(4 处):`fork.cpp` 65/91(CoW 共享 PTE)+ `page_fault.cpp` 315(file fault map cache page)+ `sys_shm.cpp` 199(shmat)→ 留 `pte_count_inc`。
- **`pte_count_inc` 改 `refcount_inc`**(1 处,cache own):`page_cache.cpp` 95(get_page,批 2 已改 CachePhysRef::alloc 内部,确认)。
- **`pte_count_dec_and_test` 拆 `pte_count_dec` + `refcount_dec_and_test`**(7 处):
  - `address_space.cpp` 193(`free_subtree`):`pte_count_dec` + `refcount_dec_and_test`(归 0 free)。
  - `process_new.cpp` 119(brk):同。
  - `execve.cpp` 126(`clear_user_mappings`):同。
  - `sys_mmap.cpp` 188(munmap,B 已 `dec_and_test`,批 3 拆)。
  - `process.cpp` 118(`handle_cow_fault`):`pte_count_dec`(旧)+ `refcount_dec_and_test`(旧归 0)+ 新 `AnonPhysRef`(批 2/3)。
  - `sys_shm.cpp` 193/247:同。

**语义切换关键**:`pte_count_dec` 不 free。free 由 `refcount_dec_and_test`(归 0)。cache 持 refcount(`refcount_inc` cache own)→ teardown `refcount_dec` 不归 0(cache own 保)→ cache page 不被 free。

验证:`run-kernel-test-all` + **GUI gcc 端到端**(两次 gcc + ./a.out,lto 不 corrupt 回归)。

### 批 4:页表 PageTablePhysRef + 收尾 + free private

**目标**:页表类型化 + slab/elf/shm/dma 持 PhysRef + 删旧。

改动:
- 页表(PML4/PDPT/PD/PT):`PageTablePhysRef`(`address_space`/`vmm`/`fork`/`clone`/`ap_main` 持)。`~AddressSpace`/`operator=`/`free_subtree`/`walk_level` 用 `~PageTablePhysRef`(替代 raw `free_page`)。
- `slab.cpp`(grow_cache/析构):PhysRef(新 `SlabTag` 或 `AnonPageTag`)。
- `elf_load.cpp`(PT_LOAD)+ `process_new.cpp`(brk)+ `task_builder.cpp`(stack)+ `user_launch.cpp`(用户栈)+ `execve.cpp`(sigret)+ `sys_shm.cpp`(shmget/shmat/shmdt/shmctl)+ `dma_pool.cpp`(DMA):类型化 PhysRef。
- 删 raw `alloc_page`/`free_page` public(全迁移完)+ 旧 `pte_count` 残留。
- `free_page` private 收尾(批 3 已,批 4 确认全 caller 不直接 free,编译错即达标)。

验证:`run-kernel-test-all` + `test_host` + GUI gcc 端到端。

## 关键设计(别偏离)

- **`PhysRef<Tag>`** 模板(move-only/share/~dec refcount)。Tag:`CachePage`/`AnonPage`/`PageTable`。编译期禁跨 owner(`PhysRef<CachePageTag>` ≠ `PhysRef<AnonPageTag>`)。
- **PMM 分离 `refcount_storage_`(ownership)+ `pte_count_storage_`(user PTE)**。`alloc` 设 `refcount=1`/`pte_count=0`。
- **`free_page` private**(friend `PhysRef<Tag>`)。caller 直接 `free_page(phys)` **编译错**。这是 sys_munmap bug 的根治。
- **`pte_count_dec` 不 free**(纯 PTE)。free 由 `refcount_dec_and_test`(归 0)。
- **raw phys escape**(`PhysRef::phys()` 只读):页表 entry / DMA / direct map 计算。页表 entry 存 raw + `pte_count_inc`(entry = 一份 PTE)。
- **Linux 模型**:`page->_refcount`(ownership)+ `_mapcount`(user PTE)。CinuxOS 对齐。

## 已知陷阱(批 1 踩的,别重蹈)

1. **`cmake --fresh` 删 `CMakeCache.txt`** → `CINUX_BUILD_TESTS` 丢(`cmake/options.cmake:77` option 默认 OFF)→ `mini_kernel_test` 不生成 → `run-kernel-test-all` 报 "No rule to make target mini_kernel_test"。修:`cmake -B build -DCINUX_BUILD_TESTS=ON`。**别随便 --fresh**。
2. **zsh `$(grep ...)` 不分词**(整串当一个文件名)→ sed 报 "can't read"。用 `grep -rln ... | xargs sed -i`。
3. **clang-format 别跑 `CMakeLists.txt`**(memory `clang-format-skip-cmake-txt`:format 对 .txt 按 C 重排 → CMake error)。只 format `.cpp/.hpp/.h`。
4. **别手动 `rm build/kernel/CMakeFiles/*.dir`**(删 build.make → "No rule")。要清用 `cmake -B build --fresh`(然后记得 `-DCINUX_BUILD_TESTS=ON`)。
5. **sed 改名会把 CMakeLists 里的文件名引用也改**(`test_pmm_mapcount.cpp` → `pte_count`,但文件名没改 → 找不到源)。配套 `git mv` 文件名。

## 验证(每批,绿才 commit)

```bash
# 全量编译(改公共 PMM API 必须全量)
cmake --build build -j$(nproc)

# 两 leg(基线:单核 1108/0 + SMP bb 14/14)
timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc) > /tmp/run.log 2>&1
/usr/bin/grep -aoE "Tests: [0-9]+ passed, [0-9]+ failed|bb batch: [0-9]+/[0-9]+ PASS" /tmp/run.log | tail -3

# host(批 2+)
cmake --build build --target test_host

# GUI gcc 端到端(批 3+,lto 不 corrupt 回归)
rm -f build/rootfs-gcc.ext2  # 强制重 assemble(避免 stale corrupt rootfs 误判)
cmake --build build --target assemble-gcc-rootfs
timeout 200 cmake --build build --target run-single > /tmp/diag.log 2>&1
# VNC shell: gcc -fno-pie -no-pie hello.c && ./a.out && gcc -fno-pie -no-pie hello.c && ./a.out
# 别重 assemble,跑完 debugfs 查 crt1.o + lto_plugin 仍 relocatable/valid:
#   debugfs -R "dump /usr/lib/gcc/.../liblto_plugin.so /tmp/x" build/rootfs-gcc.ext2 && file /tmp/x

# 行数 hook(pre-commit 自动跑,本地可手测)
python3 scripts/check_line_limits.py --hpp 500
```

## 下个 AI 接上步骤

1. 读这条 + plan(`.claude/plans/mossy-sparking-wigderson.md`)+ memory(`mm-mapcount-munmap-cache-phys`)。
2. `/resume`(PLAN.md + git log)确认在 `feat/f-usability-b4-gpp`、HEAD `99bc982`、批 1 已绿。
3. **批 2**(cache + VMA `CachePhysRef`):改 page_cache + vma + file_fault。验证。commit。
4. **批 3**(PMM 拆 refcount/pte_count + 12 caller 拆 + PhysRef 切):验证(**含 GUI gcc**)。commit。
5. **批 4**(页表 `PageTablePhysRef` + slab/elf/shm/dma + 删旧 + free private 收尾):验证。commit。
6. 三 commit(c6abfff/f06ea6b/99bc982)+ 批 2-4 都绿 → 用户 push。

## follow-up(批 4 后)

- cache eviction(B/f06ea6b 后 cache grow-only,内存增长):refcount 让 cache release 可行(`~CachePhysRef` → refcount dec → 归 0 free)。批 4 后可加 LRU。
- PhysRef `<memory>` 自写(memory `kernel-can-use-std-smart-ptr`:<memory> 受限 EXEMPT,但 PhysRef 用原子 `__atomic_*` 自写更可控)。

---

接 [[mm-mapcount-munmap-cache-phys]](B 治本)/ [[ext2-inode-cache-aliasing-handoff]](c6abfff)/ [[gui-gcc-gp-tmp-create]](gcc 闭环)。
方法论 [[dont-ask-whether-to-investigate]] [[parallel-agents-rigorous-methodology]]。
