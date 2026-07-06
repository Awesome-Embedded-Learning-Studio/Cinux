# F-GUI-USERSPACE 批0 立项 + 批1a fb mmap mm 基建

日期 2026-07-06。worktree `worktree-gui-userspace`(从干净 `origin/main` `bb2e9ac`,Cinux-GUI pin `0b0c135` F13-B 收官)。本仓主 worktree 的 VirtIO bisect 现场不动,GUI 用户态化在独立 worktree 并行。

## 背景

GUI 现跑**内核线程**(`gui_worker` ring0,visor 时代遗留)。本弧迁**用户态进程**:spawn 自然(PTY 批2 `fork+execve /bin/sh` 一行代码)+ 安全隔离(GUI bug 崩进程不崩内核,刚治的 IST bug 教训)+ Linux 范式(X/Wayland 全用户态)+ 基建齐(F10 ring3 + F2 mmap + F8 IPC + F9 安全全 ✅)。资产:Cinux-GUI core host-neutral(子模块 `0b0c135`),`host/linux_fbdev_main.cpp`(用户态 fbdev host)是 CinuxOS host 参考样板。

## 批0 立项(`8785b96`,docs-only)

PLAN 段 + ROADMAP 横切弧行/焦点行 + `todo/f-gui-userspace/README.md`(范围栅栏 + 4 批规划 + 批1 设计缝点)。

## 批1a fb mmap mm 基建(`d19524d`)

**摸底**:现有 mmap 只 anonymous(懒分配)+ file-backed(page cache)两种,**无 device phys 映射机制**(无 remap_pfn / VMA phys 字段 / vm_ops->fault)。fb 物理地址来自 `BootInfo.fb_addr`(VBE PhysBasePtr),`Framebuffer::init` 用 2MB huge page 映射到 `KMEM_FB_BASE`(cached),`fb_phys` 是局部变量未导出,无 `/dev/fb0` 节点。`InodeOps` 无 mmap 钩子。

**缝点**(9 处):
1. `vma.hpp`:VMA 加 `phys_base` 字段 + `VmaFlags::IoPhys`(`sizeof(VMA)` 56→64,断言同步)。
2. `inode.hpp`/`inode.cpp`:`InodeOps::mmap` 钩子(默认 NotImplemented)。
3. `page_fault.cpp` `handle_pf`:IoPhys 分支(在 file-backed 前)——`phys = phys_base + (page - start)`,带 `FLAG_PCD` uncached,不走 PMM/page cache/pte_count。
4. `sys_mmap.cpp`:接设备(调 `ops->mmap` 拿 phys 建 IoPhys VMA)+ `sys_munmap` IoPhys 分流(只 unmap PTE 不 `pte_count_dec_and_test`)。
5. `fork.cpp`:`copy_page_table_level` 对 `FLAG_PCD` PTE 跳过 CoW + pte_count(共享设备内存,writable 保持)+ VMA 复制带 `phys_base`。
6. `framebuffer.hpp/cpp`:`phys_base_`/`size()`/`phys_base()` accessor + `system_framebuffer()` singleton。
7. `fb_dev.hpp/cpp`(新):`FramebufferDevOps`(`mmap` 返 fb phys + 边界校验;`ioctl FBIOGET_SCREENINFO`)+ `framebuffer_dev_ops()` 全局。
8. `devfs_init.cpp`:注册 `/dev/fb0`。
9. `main.cpp`:`set_system_framebuffer(&fb)`(fb init 后)。
10. `drivers/CMakeLists.txt`:`video/fb_dev.cpp`。

**踩坑**:
- ⭐ **stale clangd**:加 `IoPhys`/`phys_base`/`mmap` 后,clangd 持续报 "No member"(没重解析 vma.hpp/inode.hpp)。**gcc 编译为准**——big_kernel + big_kernel_test 全量绿证明改动对。memory 已记"clangd stale(gcc 权威)"。
- ⭐ **IoPhys map 失败不 fall-through**:self-review 发现 IoPhys fault 分支 `map_nolock` 失败(OOM)时原 fall-through 到 anonymous,会用 RAM 页 backing 设备 VMA(语义错)。改 return 不服务(OOM 解除重试或 panic,同 anonymous OOM)。
- ⭐ **fork CoW 污染 fb**:fork 的 `copy_page_table_level` 对 writable PTE 清 writable + 标 COW(父子)。fb 的 IoPhys PTE 是 writable+PCD,若不特殊处理,父进程(GUI server)写 fb 会触发 CoW 分配新 RAM → 不写真 fb → GUI 崩。解法:leaf PTE 带 `FLAG_PCD` 的跳过 CoW(直接复制共享,设备内存非 PMM,无 pte_count)。`FLAG_PCD` 是 IoPhys 唯一标识(其他用户映射不用 PCD)。
- **`copy_to_user` 命名空间**:`cinux::user::copy_to_user`(非全局),fb_dev.cpp 首次编译报 not declared,加限定。
- **BUILD_TESTS**:worktree fresh configure 默认 `CINUX_BUILD_TESTS` 未设 → `mini_kernel_test` target 不生成 → `cinux_test.img` 缺依赖。Claude 跑 console gate 要 `-DCINUX_BUILD_TESTS=ON`(memory:主 build 是 GUI 验证 BUILD_TESTS=OFF,Claude 验证别处 ON)。

**验证**:
- big_kernel + big_kernel_test + mini_kernel_test gcc 全量绿(stale clangd 错全假)。
- `run-kernel-test-all` 两腿 **917 passed, 0 failed** + SMP AP1 wake + readback PASS(零回归,不破现有 mm/fork/mmap/DevFs)。

## follow-up

- **批1b**:fb mmap 端到端 ring3 smoke(open/mmap `/dev/fb0` 写屏 + cinux-exit)真触发 IoPhys fault 路径(回归 917/0 不触发 IoPhys,逻辑靠 review)。走 musl hello smoke 链:程序(`tools/musl/`)+ CMake + `create_ext2_disk.sh` 注入 + `kernel/test/main_test.cpp` `fb_mmap_smoke_entry` + `CINUX_FB_MMAP_SMOKE` option。
- **批2-4**:输入到用户态(`/dev/input/event*`)→ 用户态 GUI 进程(Cinux-GUI core + CinuxOS host 抄 `linux_fbdev_main`)→ `gui_worker` 退役。
- 缓存策略现 `FLAG_PCD` uncached(跟 MMIO 一致,真硬件安全,QEMU 无损);WC/PAT 留 follow-up。fb 多进程留 follow-up。

## 关键资产位置

- 批1 mm 改动:`vma.hpp` / `page_fault.cpp` / `sys_mmap.cpp` / `fork.cpp` / `inode.{hpp,cpp}`。
- fb 设备:`drivers/video/fb_dev.{hpp,cpp}` + `framebuffer.{hpp,cpp}` + `devfs_init.cpp`。
- 规划:`todo/f-gui-userspace/README.md` + PLAN「🔄 F-GUI-USERSPACE」段。
