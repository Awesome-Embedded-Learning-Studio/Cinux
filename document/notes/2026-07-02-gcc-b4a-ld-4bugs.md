# 2026-07-02 — 批4-a ld 自举 4 层 bug 排查(file-backed mmap 页生命周期 + ext2 unlink)

> GCC 自举批4-a(GCC 工具链 glibc 动态)。分支 `feat/b4-gcc-toolchain`,7 commit `c7c1ff5→13f2c7e` 后续本批修正。
> ⚠️ **批4-a 收官 note(`2026-07-02-gcc-b4a-as-ld-selfhost.md`)「./hello 跑通 Hello from GCC」曾误判**:当时实测 `./hello` 输出 **"Hello from musl on CinuxOS!"** —— 盘上预放的 musl 静态 hello,**非 ld 产物**。交接记的「ld exit SIGSEGV @0x240613308」展开成多层独立 bug(file-backed mmap 页生命周期 + ext2 + PageCache/uaccess/toolchain staging)。本 note 记排查与最终修复。

## 起点:ld SIGSEGV @0x...308(三次低 12 位固定)

交接说 ld 链接 hello 后 exit cleanup SIGSEGV。重跑实测:
- 崩溃地址随 ASLR 漂移(`0x240613308` / `0x25E286308` / `0x23B0AB308` / `0x2733DE308`)。
- **三次低 12 位都是 `0x308`**(ASLR 只动页基址,低 12 位是页内偏移,固定)。
- `rip=0x100126AD`(ldso + 0x126AD)。
- 且崩溃**不在 ld exit cleanup**,而在 **ldso 加载 libbfd 后立即**(handoff 的「exit cleanup mmap arena」方向不准)。

## 诊断方法学(四件套,定位「数据驱动 garbage 指针」极有效)

1. **fault dump**(exception_handlers.cpp segfault 分支临时加):`InterruptFrame` 全套 GP 寄存器(rdi/rdx/rax…)+ rax 指向结构的 40 字节(direct map 读 phys)+ 全 VMA(start/end/flags/file_offset/backing)。
2. **page-cache trace**(page_cache.cpp get_page 临时加):HIT/MISS + ino/offset/phys + HIT 时 dump 前 16 字节。
3. **objdump 用户态 ELF 偏移**:`rip=0x100126AD` → ldso+0x126AD,host `/usr/lib/ld-linux-x86-64.so.2` objdump 看指令(`mov (%rdx),%eax` 解引用)。
4. **readelf/xxd 对比文件真实字节 vs 内存读出**:一锤定音(文件 `DT_GNU_HASH.d_ptr=0x308` vs 内存 `0x2733DE308`)。

## bug1:file-backed MAP_PRIVATE RW demand-page 无 CoW → SEGV(11)

**证据链**:
- fault dump 寄存器:`rdi=0x1003D990`(ldso .bss,`_rtld_global` 之类);`rax=*(rdi+0x2d8)`(指向 libbfd data 段某 `Elf64_Dyn`);`rdx=*(rax+8)=` 崩溃地址(9GB 垃圾)。
- rax 指向的 Elf64_Dyn:tag=`0x6ffffef5`(DT_GNU_HASH ✓ 正确)+ val=`0x2733DE308`(✗ 垃圾)。
- readelf/xxd 定位:这库是 **libbfd-2.46.0.so**(LOAD off `0/0x14000/0x107000/0x141000` 匹配);`rax` 相对 base = `0x156138` = `.dynamic` 段 DT_GNU_HASH Dyn。**文件 val=`0x308`**(GNU_HASH 表 vaddr);内存 val=`0x2733DE308` = `as_base + 0x308`(as 重定位后值)。
- page-cache trace 证实:as MISS 读 libbfd `0x156000` → phys `0xbfcbb000`(内容 `0x308`);ld HIT **同 phys `0xbfcbb000`** → 读到 as 写的 `0x2733DE308`(stale)。

**根因**:`handle_pf` file-backed 分支 demand-page 时,MAP_PRIVATE Write 的 VMA(libbfd/libc `.data` RW)把**共享 page cache 物理页**直接映射成 WRITABLE(无 CoW)。as 的 ldso 重定位 libbfd DYNAMIC(写 `DT_GNU_HASH.d_ptr = as_base + 0x308`)直接污染**共享 page cache** → ld 后续 HIT 同物理页读 stale → 解引用 9GB → SEGV。

**修复**(`exception_handlers.cpp` handle_pf file-backed 分支):`private_writable`(Write && !Shared)的 file-backed VMA,demand-page 时 `alloc_page_locked` + `memcpy`(从 `DIRECT_MAP_BASE+cache_phys` 到 cow_phys)复制 page cache 页到私有 anonymous 页,映射副本 RW + `g_page_cache.release(cache ref)`;RO / MAP_SHARED 仍直接共享 cache 页。

## bug2:进程退出 free 共享 page cache 物理页 → exit 127

bug1 修后 ld exit **127**。page-cache trace:
- as MISS 读 libbfd header(off=0)phys=`0xbfcbb000`,bytes=`7f 45 4c 46`(ELF magic ✓)。
- ld HIT **同 phys=`0xbfcbb000`**,bytes=`a8 a0 18 00…`(✗ garbage)。

**同物理页,as 读 ELF magic,ld 读 garbage** —— page cache 物理页在 as→ld 之间被覆盖。

**根因**:`handle_pf` file-backed demand-page 映射 page cache 页后**没 `mapcount_inc`**。`AddressSpace::~AddressSpace()` → `free_subtree` leaf 用 `mapcount_dec_and_test(data_phys)`,cache 页 mapcount 到 0 → **free 共享 page cache 物理页** → PMM 重分配(被 as 写 hello.o / 别的覆盖)→ page cache `CachedPage.phys` 指 stale → 下进程 HIT 读 garbage。

**mapcount 语义**:`alloc_page` 设 `mapcount=1`。page cache 语境这「1」代表「cache 拥有」(不是映射);anonymous 语境代表「唯一映射」。差别靠 demand-page 是否 `mapcount_inc`(file-backed inc,anonymous 不 inc)。

**修复**(同 file-backed 分支):demand-page `map_nolock` 后,若 `map_phys == cache phys`(非 CoW)`g_pmm.mapcount_inc(map_phys)`。cache 页 mapcount = 1(cache)+ N(映射),退出 dec 后 ≥1,不 free。CoW 副本(`map_phys≠cache`)不 inc(私有,退出该 free)。

## bug3:ext2 unlink singly-indirect free_block 覆盖 block_buf_ → exit 1 + free_block garbage

bug1+2 修后 ld exit **1** + 大量 `[EXT2] free_block: block 262144(0x40000) group out of range`。garbage 块号(`262144` / `33562495` / `28704777` 等)。

**根因**(Agent 辅助分析 + 实证):`ext2_directory.cpp` unlink 遍历 singly-indirect 释放时(原 406-414 行),`free_block` 内部 `read_block(bitmap)`+`write_block` 覆盖 per-FS 单 `block_buf_`;循环指针 `indirect`(指向同 buf)第一次 free_block 后,`indirect[i]` 读的是 **bitmap 字节 reinterpret 成 uint32**(=`0x40000` 等典型 bitmap 模式)→ "group out of range"。direct 释放从 inode 结构读(不经 buf)所以 as/hello.o 小文件不触发;ld/hello 大(indirect)触发。

**额外发现**:unlink **完全缺 double-indirect(i_block[13])释放**(>268KB 文件 unlink 永久泄漏整个 double-indirect 子树)。

**修复**(`ext2_directory.cpp` unlink + `ext2.hpp`):singly-indirect 把指针 snapshot 到 Ext2 成员 `unlink_ptr_buf_[1024]`(不能栈,`-Wframe-larger-than=1024`)再 free;补 double-indirect 释放(两层 snapshot:`unlink_ptr_buf_` 顶层 + `unlink_child_buf_` 子层,嵌套不能共用一个 buf)。加 ext2.hpp 两成员 + ext2_directory.cpp include string.hpp(memcpy)。

## bug4:ld exit 1 / file not recognized / cannot find -lc — ✅已解

bug1-3 修后:free_block garbage 消除,page cache 不污染(libbfd header 保 ELF magic)。后续所谓 "bug4" 继续拆成 3 个屏蔽层:

1. **Ext2 write 绕过 PageCache**:`as` 直写磁盘块生成 `/hello.o`,但 PageCache 里可能仍有 create 后的旧零页;`ld` 随后 mmap/read 同 inode 命中 stale cached page,报 `file not recognized`。修:`PageCache::invalidate_range()` 对写入范围内已缓存页原地重读;`Ext2FileOps::write()` 成功写入后调用。
2. **uaccess extable 早于懒缺页**:`read(fd, user_buf, 11264)` 的目标 buffer 跨到尚未触达的 malloc/mmap 页时,`copy_to_user rep movsb` 在 ring0 #PF,旧逻辑先走 `__ex_table` fixup 直接 `-EFAULT`,没有机会 demand-page;bfd 读 `libgcc.a` 得到短读/错误后报 `file not recognized`。修:#PF handler 对 kernel-mode uaccess 的 not-present + user addr + 有效 VMA fault,先让正常 demand paging 填页并恢复 `rep movsb`;无 VMA/权限错仍走 extable `-EFAULT`。
3. **toolchain staging 漏 link-time libc**:ld 进入业务后报 `cannot find -lc`。运行时 `libc.so.6` 已有,但 link-time `/usr/lib/libc.so`(GNU ld script)、`libc_nonshared.a`、`libc.a` 未装盘。修:`tools/gcc-toolchain/extract.sh` 同步复制三者。

最终验证:手动 TCG 同一测试镜像 `1098 passed,0 failed` + `[B4-B3] ld /hello.o -o /hello PASS (status=0)` + `Hello from GCC!` + `[B4-B3] ./hello (self-host) PASS (status=0)`。

## 进展层级

`SEGV(11) → exit 127 → exit 1 → file not recognized → cannot find -lc → PASS`。每层修复实证有效:libbfd header 跨进程保 `7f 45 4c 46`、free_block garbage 消除、`/hello.o` 缓存刷新、`libgcc.a` 大读不再 `-EFAULT`、link-time libc 可被 ld 找到。

## 教训留存

1. **file-backed mmap 页生命周期**:page cache 物理页跨进程共享,MAP_PRIVATE Write 必须 CoW(否则写污染共享 cache);demand-page 映射要 `mapcount_inc`(否则进程退出 free 共享页)。`alloc_page mapcount=1` 的语义靠调用方定(page cache=cache 拥有,anonymous=唯一映射)。
2. **ext2 单 `block_buf_` 设计**:per-FS 单 scratch buffer,任何遍历中途调 `free_block`/`read_block`/`write_block` 都会覆盖 buf;遍历间接块指针必须先 snapshot 到独立 buffer。`free_block` 内部 I/O 覆盖 buf 是隐藏陷阱(读路径 get_or_alloc_block 的 read-modify-write 顺序 F10-M2 处理过,但 unlink 释放路径漏了)。
3. **write-through 与 PageCache 必须有一致性点**:当前 ext2 write 直写磁盘,read/mmap 走 PageCache;只要同 inode 先 cache 后 write,后续读就会 stale。短期用 invalidate+in-place refresh,长期可演进 write-through/writeback。
4. **uaccess #PF 不能把有效 VMA 懒缺页当 EFAULT**:extable 是坏地址恢复,不是 lazy allocation 的替代。not-present + valid VMA 要先 demand-page。
5. **诊断四件套**:fault dump 寄存器 + direct map 读物理页字节 + page-cache get_page trace(ino/off/phys/bytes)+ objdump 用户态 ELF 偏移 —— 对「数据驱动 garbage 指针」类 bug 极有效。
6. **smoke PASS ≠ 真 PASS**:`./hello` PASS 但输出是 musl 静态("Hello from musl"),没核对输出内容,差点当 ld 自举成功。glibc 动态自举 smoke 必须核对 "Hello from GCC"(ld 产物),不能只看 exit 0。

## 状态(2026-07-02 会话三末)

- 修复范围:CoW + mapcount + ext2 unlink snapshot/double-indirect + PageCache invalidate + uaccess valid-VMA demand-page + libc link artifacts。
- 临时诊断仪器已撤。
- 批4-a as+ld 自举重新成立:`ld /hello.o -o /hello` PASS,新产物 `./hello` 输出 `Hello from GCC!`。

详见批4-a 收官 note `2026-07-02-gcc-b4a-as-ld-selfhost.md`。
