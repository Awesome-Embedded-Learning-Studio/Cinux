# 2026-07-05 — B2 ELF PT_LOAD 改 Linux 风格 lazy demand paging(cc1 加载 22s→0s)

## 背景

B1(commit `84288cd` + `5a43320`)定位 gcc 编译「卡炸」根因到 `elf_load.cpp` eager PT_LOAD
loop:execve 加载 cc1(47 MB)per-page `alloc_page`+read 磁盘+map,~12000 页 × AHCI I/O =
**22 s stall**(用户 GUI 曲线 t=10→32,PMM 每秒掉 500 页)。memory `gui-gcc-gp-tmp-create`
已登记 follow-up「elf_load eager PT_LOAD」。

B2 = 改 lazy demand paging(execve 只建 VMA,页按需 #PF 填)。CinuxOS 早是 lazy 范式
(`mmap`/`brk`,F2 memory `f2-lazy-allocation-paradigm`),**execve 是唯一没 lazy 的路径**。

## 设计(Linux `load_elf_binary` 风格)

每 PT_LOAD 段三分(对齐 Linux `elf_map` + `padzero` + `set_brk`):

- **file-backed VMA `[seg_start, file_pages_end)`** —— 全 file 页(`p_filesz` 页对齐 down),
  lazy demand paging(`handle_pf` → `PageCache::get_page`)。= Linux `elf_map`。cc1 的
  `.text`/`.rodata`(`p_filesz==p_memsz` 无 BSS)主体,47 MB → 0 I/O 直到访问。
- **eager 跨边界页 `[file_pages_end, file_pages_end+PAGE_SIZE)`** —— `p_filesz` 尾 + 同页
  BSS 零填(精确 read `p_filesz` 范围 + 零)。= Linux `padzero`。lazy `get_page` 按
  `inode->size` 读(含段后 section headers)会污染同页 BSS,所以这页必须 eager。
- **eager 匿名 BSS `[跨边界页后, seg_end)`** —— 纯 BSS 页零填。= Linux `set_brk`。

复用 main 基建(不重造):`handle_pf` file-backed 路径(page_fault.cpp:256-338)+
`PageCache::get_page` EOF 零填 + CoW + VMA backing refcount(`vma.cpp release_backing`,
sys_mmap 范式)。

## 调试历程(两坑)

1. **BSS 污染**:首版「整段 file-backed VMA + 匿名 BSS VMA」(`file_end_va=round_up p_filesz`)。
   musl hello `.data` 段(`p_vaddr=0x403fd0` 非页对齐,`p_filesz=0x40 < p_memsz=0x2a8`)整段
   file-backed → 同页 BSS(VA 0x404070)走 `get_page(inode, 0x3000)` 按 `inode->size` 读,
   读到段后 section headers(`__libc+0x10` 读到 1 而非 0)→ `__copy_tls` 解引用 NULL → segfault。
   ext2 read 短读到 `i_size` 没错,`get_page` memset+read 也没错,错在 `get_page` 不知 `p_filesz`
   边界(按 `inode->size` 整文件)。**这是 plan 假设「p_filesz 页对齐」破** —— 实际 gcc/binutils
   ELF `.data` `p_filesz` 几乎都非页对齐。中间想过「条件 lazy(无 BSS 段 lazy / 有 BSS 段 eager)」,
   被用户指出是 workaround(整个 `.data` 段 eager 没必要)。正解 = Linux 风格三分(只跨边界页
   eager = padzero)。
2. **VMA merge bug**:三分版 musl hello 通(静态,无 interp)但 busybox(动态 musl)挂 ——
   rip=0x4BCB3B(`.text`)执行零页(`0x00 0x00 = add %al,(%rax)` 写 NULL+6)。根因:lazy VMA
   (段2 `.text` `[0x401000,0x4F6000)` file-backed)+ eager VMA(`[0x4F6000,0x4F7000)` `.text` 尾
   跨边界)**同 flag(R+E)**,`vmas().insert` merge 它们,merge 调 `release_backing` 丢 file-backed
   的 inode ref,合并 VMA 变匿名 → `.text` demand PF 走匿名零页 → 执行零页 crash。musl hello
   侥幸没炸是因 `.text` 段 `p_filesz<1页` 无 lazy VMA(全 eager)。**修:eager 尾 VMA 加
   `VmaFlags::Anonymous`**,与 file-backed VMA 区分,阻止 merge。

## 验证

- **回归**:`run-kernel-test-all` 两 leg **EXIT=0, 2290 PASS / 0 FAIL**(musl hello / busybox /
  test_file_mmap / fork-exec 全过)。
- **usability**:`run-buildroot-usability`(EXIT=0)gate **PASS**(gcc-compile-run + gpp-compile-run),
  cc1/cc1plus 加载 **0 s**(loading→loaded 同 `[MEM]` 时刻),gcc 编译 31s→22.6s(B1 eager vs B2 lazy)。
  PMM drop 25888 页 = cc1+cc1plus 实际访问页 demand PF(合理,编译器访问多页)。

## 成果

- **cc1 加载 22 s → 0 s**(主要目标,卡顿主因消除)。
- gcc 编译 ~31 s → ~22.6 s。
- memory `gui-gcc-gp-tmp-create` follow-up「elf_load eager PT_LOAD」闭环。
- memory `gcc-compile-stutter-perf` B2 闭环(观测 → 定位 → 修)。

## follow-up

- page cache LRU evict(独立债;B1 证伪为卡顿主因,但 cache 仍 grow-only)。
- Linux 精确 padzero(CinuxOS 跨边界页 eager 简化,1 页 eager 等价;若 ELF 段密集可优化)。
