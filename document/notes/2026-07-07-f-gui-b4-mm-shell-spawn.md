# F-GUI 批4:mm SMP race 修复 + shell spawn PTY 修复(交接落地)

**日期**:2026-07-07　**分支**:`worktree-gui-userspace`(本地,未 push)　**前置**:`b45aed8`(GUI 收尾弧)/ `bd90833`(keyboard MSI-X)　**本批 commit**:`96bd1ae`(mm)+ `e0040a4`(fs)

批4 worktree dirty 落地交接。keyboard + GUI 收尾弧已 commit(`b45aed8`)后,worktree 还 dirty 9 文件 —— 上一轮会话留下的两类验证过但未 commit 的修复 + 一批调试 trace。本批清 trace、拆 2 个 commit、console gate 重验。

## 改动

**commit 1 `96bd1ae` —— mm SMP buddy race 修复 + buddy OOB DIAG**:
- `pmm.hpp`: `alloc_page_locked` / `free_page_locked` 从 **public 移 private**(编译期禁止外部绕锁调用 —— 原 public 暴露正是本次 bug 的设计根因)
- `vmm.cpp` `walk_level` 两处 + `page_fault.cpp` CoW/anon 两处: `alloc_page_locked()` → `alloc_page()`(走 PMM spinlock)
- `buddy.cpp`: `set_bit` / `clear_bit` 加 narrow OOB DIAG(`kpanic` + backtrace,在 wild deref 前),抓未修的 nested-fork/PageCache corrupt 作被动监控

**commit 2 `e0040a4` —— shell spawn PTY 修复(devfs + cloning)**:
- `devfs.{cpp,hpp}`: `mount()` 建虚拟 `pts_dir_inode_`(ino `0xFFFF`,避开静态节点表 1-based 与 PTY slave `kPtyInoBase=0x1000`);`lookup_child` 两级 hop(`root→"pts"→pts_dir_inode_`;`pts_dir→"<N>"→` 重组 `"pts/N"` 调 `dynamic_lookup_` → PTY slave)
- `sys_open.cpp`: `do_openat_kernel` 加 `inode->ops->open()` cloning(mirror `do_open_kernel`),`/dev/ptmx` open 返新 PTY master inode

**trace 清理(不进 commit)**:`sys_ioctl.cpp`(`[ioctl]` dispatch trace + `kprintf.hpp` include)+ `pty_device.cpp`(`[pty]` master ioctl / PtmxOps::open 4 处 trace)删完两文件回原样。

## ⭐ 关键教训

1. **SMP buddy race 根因 = UP 假设**:`buddy.hpp` 注释 "exclusion-via-IF=0 for page-fault path" 是 **UP 假设** —— IF=0 只禁本 CPU 中断,不挡其他 CPU。SMP 下 `handle_cow_fault`(持 PMM spinlock)与 page-fault 路径的 `alloc_page_locked`(绕锁)并发进 `buddy_.alloc_order` → `find_first_set` + `clear_bit` 非原子 → 第二次 `find_first_set` 返 `kInvalidPage` → `clear_bit(kInvalidPage)` 越界 `#GP`。修法不是"加锁到 page-fault",而是**移 private 从源头禁止外部绕锁**(public 暴露 = 设计缺陷)。同族 [`mm-mapcount-munmap-cache-phys`](../../../) / `mm-physref-refcount-split`。
2. **addr2line 纠正 kallsyms 偏移**:production `#GP` RIP 经 kallsyms 看似 `alloc_order+0x9c`,addr2line 实指 `clear_bit+0x35`(kallsyms 稀疏偏移误导)。同 `mm-mapcount-munmap-cache-phys` 教训:RIP 定位以 addr2line 为准。
3. **PTY shell spawn 两坑**:① devfs flat → vfs 分量 walk 死在 `"pts"` 分量(`dynamic_lookup_` 期望整串 `"pts/N"` 拼写,不是分量)→ `open("/dev/pts/N")` 失败 → shell 继承 `/dev/console`(ino=3,serial log 铁证 `ioctl fd=0 ino=3`)→ 终端窗口空白。修:虚拟 pts 目录两级 hop。② musl `open()` 走 `sys_openat → do_openat_kernel`(非 `sys_open`),`do_openat_kernel` 缺 `inode->ops->open()` cloning → 拿 ptmx inode 本身 → TIOCGPTN 命中 ptmx `InodeOps`(NotImplemented)→ ENOTTY。修:mirror `do_open_kernel` 加 cloning。
4. **clang-format 22 vs CI 18 噪音**(本批新教训):本地 `clang-format-22` 全文件 `-i` 跑,对 HEAD(CI 18 产物)产生大量无关 reformat(include 排序、constexpr 对齐、构造函数单行化、续行换行)—— 进 commit 即污染。CI format job 已禁用(`format-ci-disabled-version-mismatch`),靠 CODING-TASTE。处理:对受污染文件 `git checkout HEAD` 回干净,只重加功能 hunk(format 前内容 = 上个 AI 手写,与 HEAD 风格一致),避免同文件混 22/18 两种格式。**别对敏感改动文件跑全文件 clang-format**。

## 残留(open,交接下个 AI)

- **mm PageCache corrupt**(主线 follow-up):nested-fork + dynamic ELF 触发(`#GP @ alloc_order`,RDX 非 canonical;backtrace `alloc_order → alloc_page → PageCache::miss_count/get_page → handle_pf`)。**新证据**:GUI shell + ls + 多次 fork/execve 后突发大量随机 scan flood(`ascii=0 scan=0xd4/0xa9/0xff` 非合法 scancode)= USB HID `report_buf` 被 silent corrupt(mm 坏物理页 → DMA buffer 乱 → HID report 乱)。**不是 GUI bug**(改动全 host 用户态,碰不到 kernel input 内存)。SMP race 修复**没覆盖**这条(nested-fork + dynamic ELF PageCache 路径还有 pte_count/refcount 不一致)。buddy OOB DIAG 留着作被动监控(console gate 验过不误报)。下个 AI 用 5 轨迹法(`mm-mapcount-munmap-cache-phys`)啃。
- **`[ECHO_TRACE]` 调试 trace 债**(`pty_device.cpp` slave TCGETS/TCSETS 两处,已在 `b45aed8`):非本批范围,生产 kernel 不该每个 PTY ioctl 都 kprintf。清掉即可(follow-up)。
- **滚轮翻动**(PointerPayload 无 wheel + terminal 无 scrollback)、**e1000 poll 改中断**:见 finale note。

## 验证

`cmake --build build --target run-kernel-test-all`(两 leg,`timeout 240`):单核 `917 passed, 0 failed` + SMP `917 passed, 0 failed`(`2 CPU(s)` leg 真跑)。无 panic / `#GP` / `#DF`;buddy OOB DIAG **无误报**(空)。trace 清理后重验 —— 行为等价(只删 kprintf)。

GUI run 未跑(铁律:GUI QEMU 用户启动;`gui-verification-user-starts-always`)。本批改动此前 GUI run 已验过通(keyboard/mouse/click/shell 全通,用户确认"真成了"),trace 清理不改逻辑。建议下轮 GUI run 复核 shell spawn。

## 关键文件

- mm: `kernel/mm/pmm.hpp`(locked 私有)、`kernel/mm/vmm.cpp`、`kernel/mm/buddy.cpp`(DIAG)、`kernel/arch/x86_64/page_fault.cpp`
- fs: `kernel/fs/devfs/devfs.{cpp,hpp}`(pts 虚拟目录)、`kernel/syscall/sys_open.cpp`(cloning)
