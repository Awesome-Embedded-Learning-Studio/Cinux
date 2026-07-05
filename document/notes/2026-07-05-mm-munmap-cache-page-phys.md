# mm mapcount / sys_munmap cache page phys 治本笔记

日期:2026-07-05
分支:feat/f-usability-b4-gpp
前篇:2026-07-04-gui-gcc-tmp-crt1-aout.md(ext2 cache 别名 → c6abfff)

## 背景

c6abfff 治 ext2 inode cache slot 别名(ld 把 a.out 写进 crt1.o)后,GUI gcc
第一次编译 + `./a.out` 成功,但**第二次 gcc 报 lto_plugin invalid ELF**:

```
/usr/bin/ld: /usr/lib/gcc/x86_64-pc-linux-gnu/16/liblto_plugin.so:
  error loading plugin: invalid ELF header
```

同模式(第一次 corrupt 邻居文件,第二次撞上),但目标从 crt1.o 变 lto_plugin.so。

## 现象

- **disk 干净**:`debugfs dump lto_plugin.so` → ELF relocatable,ino 602,size 75720。
- **CinuxOS read corrupt**:shell `hexdump lto | head -1` = `ings with octal`(不是 ELF magic `7f 45 4c 46`)。
- **漂移**:两次 hexdump lto 不同(物理页被反复写)。

disk 干净 + 内存 corrupt = **内存层 bug**,不是 ext2。

## 诊断 5 轮(逐层缩范围,每轮 trace 钉一层干净)

### v1:ext2 write + cache trace
`[W]`(write inode ino + cached_ino)+ `[GC-HIT/INS/EV]`(cache 命中/新建/回收)。
结果:write 全 `ino=676`(a.out)`cached_ino=676` 一致;cache 无 EV(不回收)。
**ext2 层干净** → 缩到 read/page cache。

### v2:read disk_inode + page cache
`[R602]`(read lto 时 disk_inode.i_block,blk0=113941 与 debugfs 一致)+ `[PG602-HIT/INS]`(page cache 命中)。
**ext2 read + page cache 干净** → 缩到 invalidate/teardown。

### v3:invalidate + read_bytes phys
`[INV]/[INV-PAGE]`(invalidate 刷的 page phys)+ `[RB602]`(read_bytes 给上层的 phys)。
a.out invalidate phys(`522x000`)≠ lto phys(`bfdcd000`);page_ino 没错位。
**invalidate + page cache 干净** → 缩到 free_subtree。

### v4:free_subtree + file fault mapcount
`[PF602]`(file fault backing=602)+ `[PF602-MC]`(inc 后 mapcount)+ `[FREE602]`(teardown dec lto phys)。
发现:`[FREE602] bfdcc000 mc_before=1 freed=1`(**cache page 被 teardown free!**)
+ 幽灵 dec:`[PF602-MC]` inc 到 2,但 `[FREE602]` dec 前只剩 1 → 中间一次 dec 没 trace。
**定位**:cache page phys 被 free_subtree free。幽灵 dec 待定。

### v5:PMM caller trace(钉死)
`[MC-INC/DEC/FREE]` pmm.cpp mapcount_inc/dec_and_test + free_page,filter
`g_lto602_phys`(动态 bfdcc000)+ `__builtin_return_address(0)`。

addr2line 解析 4 个 caller:
- `[MC-INC]` → `handle_pf`(file fault inc,合规)
- `[MC-DEC 幽灵]` → `AddressSpace::free_subtree`(2→1,不归 0)
- `[MC-FREE]` → **`sys_munmap`**(直接 free phys!)+ `free_subtree`(dec 归 0 free)

**钉死**:`sys_munmap` 直接 `free_page(bfdcc000)` 绕过 `mapcount_dec_and_test`。
bfdcc000(cache page phys)被 munmap 直接放 buddy(mapcount 还 2),free_subtree
后续两次 dec(归 0)又 free → double free → PMM 复用 bfdcc000 → lto cache page
内容变 "ings with octal"。

## root cause(精确代码)

`sys_mmap.cpp:179-185` sys_munmap:
```cpp
for (v = addr; v < addr + aligned_len; v += kPageSize) {
    const uint64_t phys = task->addr_space->translate(v);
    if (phys != 0) {
        task->addr_space->unmap(v);
        cinux::mm::g_pmm.free_page(phys);   // ← 直接 free,绕过 mapcount!
    }
}
```

注释假设 "demand-paged user pages"(匿名页),但 ld 加载完 lto_plugin 后
`munmap`,file-backed cache page phys(bfdcc000)被直接放 buddy(mapcount 还 2)。
free_subtree(进程退出)后续 dec 归 0 又 free → double free + cache page phys
被 PMM 复用 → lto 内容腐败。

## 抽象模型问题(为什么这 bug 存在 + 难排查)

1. **mapcount 语义过载**:cache own ref(alloc 设 1)+ user mapping(file_fault inc)
   + fork/CoW inc + CoW drop —— 4 种所有权混一个 int16,无中心。
2. **多路径不一致**:`free_subtree`/`execve`/`CoW` 用 `dec_and_test`(合规);
   `sys_munmap` 直接 free(违规)。规则靠每个调用方自觉,新路径(munmap 当年)漏 dec 没人察觉。
3. **cache page phys 跟 PMM mapcount 耦合**:cache 持有 bfdcc000,但 phys 能否
   free 由 PMM 决定(不看 cache)。cache 持有 ↔ PMM 不知道 → cache page dangling phys。
4. **Linux 模型分离**:`page->_refcount`(cache own,free 决定)+ `_mapcount`(user PTE)。
   teardown/munmap 只动 mapcount,不 free phys。cache release 动 refcount。CinuxOS 混在一处。

**为什么难排查**:
- 跨 4 层 aliasing(ext2 cache → page cache phys → mapcount → munmap/free_subtree)。
  单层 trace 看不到全貌,得逐层排除。
- corrupt 静默:disk 干净 + 对象合法 + ASAN 抓不到(物理页"合法地"被别人写着)。
  症状延迟(第一次 gcc 触发,第二次才撞)。
- mapcount 分散(4 调用方),double free 须 caller trace。
- 症状漂移(物理页反复写,看似 race,实际 double free 后 buddy 复用)。

## B 修复

1. **sys_munmap** 改用 `mapcount_dec_and_test`(归 0 才 free):
   ```cpp
   if (cinux::mm::g_pmm.mapcount_dec_and_test(phys)) {
       cinux::mm::g_pmm.free_page(phys);
   }
   ```
2. **get_page** alloc 后 `mapcount_inc`(cache own ref,明确永不被 teardown/munmap 干到 0):
   ```cpp
   // Cache-ownership ref: distinct from user-mapping refs that file_fault/fork/CoW
   // add and teardown/munmap drop.
   cinux::mm::g_pmm.mapcount_inc(phys);
   ```

cache own ref 永不 dec(无 cache eviction,cache grow-only)。teardown/munmap dec
不归 0(cache own 保)→ cache page phys 永不被 free → cache page 不 dangling。

## 验证

- `run-kernel-test-all` 两 leg PASS(单核 1108/0 + SMP bb 14/14,无真 panic)。
  mapcount 核心(fork/exec/mmap/CoW)不回归。
- GUI gcc 端到端:两次 `gcc -fno-pie -no-pie hello.c` + `./a.out` 都成功。
  lto_plugin 不再 corrupt。

## follow-up

- **C 重构**(分离 mapcount/refcount,Linux 模型):`page->_refcount`(cache own)+
  `_mapcount`(user PTE)。teardown/munmap 只动 mapcount,不 free phys。cache release
  动 refcount。根治整族 bug。已记 ROADMAP/DEBT。
- **内存增长**:B 后 cache page 永不 free(cache grow-only)。hobby os 可接受
  (几千 inode × 4KB = 几 MB)。C 加 eviction。

## 调试方法论(如何打日志调试 —— 这次怎么钉死的)

5 轮 trace 钉死 root cause 的过程,值得记录:

### 1. 逐层缩范围(不假设,用 trace 钉)

每轮 trace 钉一层干净,缩到下一层。**不靠推理假设,用 trace 数据验证**:

- v1 ext2(write/cache)→ 干净 → 缩 read/page cache
- v2 read disk_inode + page cache → 干净 → 缩 invalidate/teardown
- v3 invalidate + read_bytes phys → 干净 → 缩 free_subtree
- v4 free_subtree + file fault mapcount → 发现 cache page 被 free + 幽灵 dec → 缩 PMM caller
- v5 PMM caller trace → 钉死 sys_munmap 直接 free

**每轮一个目标**(排除一层),不堆 trace(堆了会刷屏 + 改时序 + 自己也看不清)。

### 2. filter 关键对象(不刷屏)

只打相关对象(ino=602 lto + `g_lto602_phys` 动态 bfdcc000)。全打会刷屏(kprintf 同步
串口)+ 改时序(race 消失)。

```cpp
if (inode->ino == 602) { kprintf(...); }            // 只 lto
if (phys == g_lto602_phys) { kprintf(...); }        // 只 bfdcc000
```

`g_lto602_phys` 动态获取(`[PG602-INS] off==0` 设),不硬编码 phys(每跑变)。

### 3. caller trace(多路径区分)

`mapcount_dec_and_test` 有 4 调用方(free_subtree / execve / CoW / shm),用
`__builtin_return_address(0)` + addr2line/kallsyms 区分:

```cpp
kprintf("[MC-DEC] phys=%lx result=%d caller=%p\n",
        phys, r, __builtin_return_address(0));
```

addr2line 解析(比 kallsyms 准,kallsyms 符号稀疏偏移大):
```bash
addr2line -e build/kernel/big/big_kernel -C -f 0xffffffff81032802
# cinux::mm::AddressSpace::free_subtree
```

### 4. 现场对比(disk vs 内存)

disk debugfs vs CinuxOS read(hexdump):
- disk 干净 + read corrupt = **内存层 bug**(本次)。
- 都 corrupt = ext2 disk 层 bug(c6abfff 案例)。

```bash
debugfs -R "dump /usr/lib/.../lto_plugin.so /tmp/x" rootfs.ext2 && file /tmp/x   # disk
# VNC shell: hexdump -C /usr/lib/.../lto_plugin.so | head -1                     # 内存
```

### 5. 保留现场(用户 VNC 不退出)

shell 能查(hexdump / ls -i / wc)+ 我远程 debugfs 查 disk。诊断版 build 用户跑,
trace 进串口 log(`/tmp/diag.log`),我远程 grep/awk 分析。现场不重 build 不清。

### 6. 不猜测,用 trace 钉

每层假设用 trace 验证:
- v1 我猜 ext2 cache(刚治),trace 显示 ext2 干净 → 缩。
- v4 我猜 VMA dangling,trace 显示 mapcount 平衡 bug → 钉。

不靠推理,用数据。同 [[dont-ask-whether-to-investigate]] + [[gcc-selfhost-handoff]] UB 协议。

### 7. 数据结构对齐(符号解析)

caller va → 函数名:
- `addr2line -e ELF -C -f`(最准,需 debug info)。
- `kallsyms_data_kernel.cpp`(生产内核符号表,awk 找 ≤ caller 最大 addr;符号稀疏时偏移大,误导)。
- `nm -C ELF | sort | awk`(完整符号,需排序)。

3 种交叉验证。本次 kallsyms 误导(kernel_pml4),addr2line 钉死(free_subtree)。

---

接 [[gcc-selfhost-handoff]](gcc 自举闭环)/ [[ext2-inode-cache-aliasing-handoff]](c6abfff 治 crt1.o)。
方法论 [[dont-ask-whether-to-investigate]] [[parallel-agents-rigorous-methodology]]。
