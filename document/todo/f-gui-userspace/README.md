# F-GUI-USERSPACE — GUI 用户态化

> 横切弧(同 F-USABILITY / F-VERIFY / F-ECO 档)。立项 2026-07-06。
> 分支:`worktree-gui-userspace`,从干净 `origin/main` `bb2e9ac`(含 F13-B 收官 + Cinux-GUI pin `0b0c135`)。

## 目标 / 为什么

把 GUI 从**内核线程**(`gui_worker` ring0,visor 时代遗留)迁到**用户态进程**。理由(2026-07-06 拍板,非猜测):

- **spawn 自然**:PTY 批2 要 `fork+execve /bin/sh`(用户进程)。内核线程 fork 用户态 shell 边界拧;用户态 GUI server fork+execve 是一行正常代码。
- **安全隔离**:SMAP/SMEP/F9 整套做用户态隔离,GUI(解析输入/字体/widget,代码量最大)留内核 = 白隔离。GUI bug 在用户态崩进程(重启 GUI),内核稳;在内核态 #GP/#DF 全崩(刚治的 IST bug 就是教训)。
- **Linux 范式**:X/Wayland/window-server 全用户态。内核只给设备(fbdev/evdev)+ syscall。
- **基建齐**:F10 ring3(musl/glibc)+ F2 mmap + F8 IPC(socket)+ F9 安全,全 ✅。

## 现状资产(关键)

- **Cinux-GUI core host-neutral**:子模块 `third_party/Cinux-GUI` pin `0b0c135`(F13-B 收官:DesktopIcon + cursor + clear_dirty)。core **一行不用改**就能在用户态跑——`host/linux_fbdev_main.cpp`(用户态 Linux fbdev host)+ evdev + posix_spawn 已在子模块 `host/`,是 CinuxOS host 的参考样板。
- `kernel/gui/host_cinux.cpp` = 内核态 host adapter(批4 退役)。
- `Framebuffer`(`kernel/drivers/video/framebuffer.cpp`):已映射 fb 到 `KMEM_FB_BASE`(2MB huge page,cached),但 `fb_phys` 是 `init()` 局部变量(批1 要导出),无 `/dev/fb0` 设备节点。
- 鼠标/键盘事件:`Mouse::g_event_queue_`(SPSC 128)+ `Keyboard` 队列,消费者 `gui_worker_thread`(`desktop_launch.cpp:37`→`pump`→`cinux_poll_event`)。

## 前置依赖(按难度,对应批1-4)

1. **fb mmap**(批1):`/dev/fb0` mmap 到用户态。VBE fb 现是内核 MMIO 直写;fb 驱动加 mmap vm_ops(物理帧映射到用户地址空间)。
2. **输入到用户态**(批2):kernel mouse/kbd 事件 → 用户态(evdev-like `/dev/input/event*` 或 Unix socket / SPSC 共享队列)。ISR push + 用户 pop 竞态要防。
3. **用户态 GUI 进程**(批3):Cinux-GUI core + CinuxOS host adapter(抄 `linux_fbdev_main.cpp`,换 CinuxOS syscall)。
4. **kernel `gui_worker` 退役**(批4):kernel 只剩 fb 驱动 + 输入驱动 + 进程调度。

## 批表

| 批 | 范围 | 状态 | 验证 |
|----|------|------|------|
| 0 | 立项 docs(本 README + PLAN 段 + ROADMAP 行)+ 范围栅栏 | 🔄 | docs-only |
| 1a | mm 基建：VMA `IoPhys`+`phys_base` + fault 分支（FLAG_PCD，不 PMM）+ `InodeOps::mmap` 钩子 + `sys_mmap` 接设备 + `sys_munmap` 分流 + fork PCD 跳过 CoW/带 phys_base + `fb_dev` + `/dev/fb0` 注册 | ✅ `d19524d` | big_kernel + big_kernel_test 全量绿 + 两 leg **917/0** + SMP AP wake（零回归） |
| 1b | fb mmap 端到端 ring3 smoke（open/mmap `/dev/fb0` 写屏 + cinux-exit）真触发 IoPhys fault | ⏳ | ring3 smoke PASS |
| 2 | 输入到用户态:`/dev/input/event*`(或 evdev-like 字符设备),ISR→queue→用户 read/poll,SPSC 单生产单消费保持 | ⏳ | 用户态读鼠标/键盘事件 round-trip |
| 3 | 用户态 GUI 进程:Cinux-GUI core + CinuxOS host adapter(open `/dev/fb0` + mmap + 读输入设备),`fork+execve` 启动 | ⏳ | 用户态 GUI 进程跑出桌面(图标 + 光标 + 鼠标响应) |
| 4 | kernel `gui_worker` 退役:`host_cinux.cpp` 删,kernel 只剩 fb 驱动 + 输入驱动 + 调度 | ⏳ | 桌面正常 + 内核无 gui_worker task + GUI bug 只崩进程 |

## 批1 详细设计(fb mmap 缝点)

摸底确认:mmap 现只有 anonymous(懒分配)+ file-backed(page cache)两种,**无 device/phys 映射机制**(无 remap_pfn、无 VMA phys 字段、无 vm_ops->fault)。批1 新加缝点:

1. **`Framebuffer` 加 phys accessor**(`framebuffer.{hpp,cpp}`):`init()` 存 `phys_base_ = fb_phys`,加 `uint64_t phys_base() const`、`uint64_t size() const`(= `align_up(pitch*height)`)。
2. **VMA 扩展**(`vma.hpp`):加 `uint64_t phys_base{0};` 字段 + `VmaFlags::IoPhys = 1<<7`(设备物理映射,类 Linux `VM_PFNMAP/VM_IO`)。**更新 `static_assert(sizeof(VMA)==56)` → 64**(加一个 u64)。
3. **fault handler 加 IoPhys 分支**(`arch/x86_64/page_fault.cpp` `handle_pf`):在 anonymous 分支前,`has_flag(IoPhys)` → `phys = vma->phys_base + (fault_addr - vma->start)`,带 `FLAG_PRESENT | FLAG_USER | FLAG_PCD | (Write?FLAG_WRITABLE:0) | (Exec?0:NX)` 映射。**不走 PMM、不走 page cache、不 `pte_count_inc`**(设备内存非 PMM 页)。
4. **`InodeOps::mmap` 钩子**(`fs/inode.hpp`):加 `virtual ErrorOr<uint64_t> mmap(uint64_t length, uint64_t offset)` 返回 phys_base(默认 `ENODEV`)。字符设备按需 override。
5. **`sys_mmap` 接设备**(`sys_mmap.cpp`):fd 非 anonymous + backing inode 是字符设备 → 调 `ops->mmap()` 拿 phys_base,建 `IoPhys` VMA(存 `phys_base`),不 attach file backing、不 `inode_ref`(设备内存非 page cache)。
6. **`FramebufferDevOps`**(新建 `drivers/video/fb_dev.cpp`,InodeOps):`mmap` 返回 fb phys_base + 校验 offset/length ≤ fb_size;`ioctl(FBIOGET_VSCREENINFO)` 返回 width/height/pitch/bpp。
7. **DevFs 注册**(`fs/devfs/devfs_init.cpp`):`add_node("fb0", &framebuffer_dev_ops())`。
8. **`sys_munmap` IoPhys 不 free**(`sys_mmap.cpp`):遍历 VMA,若 `IoPhys` 只 `unmap` PTE,**不 `pte_count_dec_and_test`**(设备内存不该 PMM 回收)。
9. **fork 路径带 phys_base**(`mm/address_space.cpp` fork/copy):遍历父 VMA 复制时,`IoPhys` VMA 的 `phys_base` + flag 一起带过去(子进程映射同 fb phys;设备内存共享,不 refcount)。

**缓存策略**:批1 用户态 fb 映射用 `FLAG_PCD`(uncached)。理由:① 跟其他 MMIO 区域(NVMe/xHCI/e1000 BAR)一致;② 真硬件安全(fb 不缓存,无别名);③ QEMU fb 是 RAM,PCD 对 QEMU 无性能损失(QEMU 不真模拟 cache);④ WC(Write-Combining)是 fb 最优策略但需 PAT 编程,留 follow-up。**SMP 别名风险**:批1 期间 kernel `gui_worker` 还在(写 `KMEM_FB_BASE` cached)与用户态测试程序(写用户映射 uncached)并存——但机制测不并发写,安全;批4 gui_worker 退役后无别名。SMP cached 别名留 follow-up(PCD 已消除)。

## 范围栅栏(不投机)

- **core host-neutral 不动**:Cinux-GUI 子模块 core 一行不改(Host ABI 是唯一硬缝)。批3 只写 CinuxOS host adapter(抄 `linux_fbdev_main.cpp`)。
- **批1 只做 fb mmap 机制** + 机制测,不动 gui_worker / 不迁桌面(批3-4)。批1 完成后 kernel GUI 照常跑(gui_worker 不变)。
- **PTY 批2**(`fork+execve /bin/sh`)不在本弧,是 F10/F-USABILITY 线;但本弧批3 用户态 host 的 `on_activate` stub 为它铺路(用户态 fork+execve 自然落地)。
- **X11/Wayland 远期**(不是本弧等号):X11 需 syscall 扩到 server 级 + 交叉编译 Xorg;Wayland 需 DRM/KMS 驱动(CinuxOS 无 drm)。本弧止于"自研用户态 GUI 进程"。
- **WC/PAT、SMP 别名、fb 多进程、DRM/KMS** 全留 follow-up。

## 不变量

- core(Cinux-GUI 子模块)host-neutral 不变。Host ABI(`core/host.hpp` 函数指针表,zero host includes)是唯一硬缝。
- 批1 的 mm 改动不破坏现有 anonymous/file-backed/fork-CoW/mapcount 语义(F2 收官 + C 重构治 lto_plugin 的成果)。IoPhys 是新 VMA 类型,独立分支。

## 风险 / 陷阱

- **VMA 布局断言**:`static_assert(sizeof(VMA)==56)` 加字段要同步更新(56→64),否则编译断。F-INFRA R11 锁布局。
- **IoPhys munmap 误 free**:`sys_munmap` 现对每个映射页 `pte_count_dec_and_test`,IoPhys 页是设备内存不该回收——必须按 VMA 类型分流,否则 PMM 把 fb 物理帧当普通页 free → 灾难。
- **fault 分支不 PMM**:IoPhys fault 不分配新页、不 page cache、不 pte_count_inc。漏了任一会破坏 mm 不变量。
- **fork 复制 phys_base**:fork 遍历 VMA 复制,漏带 phys_base → 子进程 IoPhys VMA phys=0 → fault 映射 phys 0(低 RAM)→ 假绿/崩。
- **SPSC 输入(批2)**:ISR push + 用户 pop 的单生产单消费假设现靠 `usb_primary_` + gui_worker 单消费;迁用户态后消费者变用户进程,要保持 SPSC 语义 + 阻塞唤醒(prepare_to_wait,同 F8-M5 poll)。
- **GUI 验证用户自启**:Claude 不 background 启 `cmake --target run`/GUI QEMU;只准备环境(GUI=ON + rebuild + assemble rootfs)告诉用户自启。console gate(run-kernel-test-all / run-buildroot-usability)Claude 正常跑。
- **VNC 避让**:多 AI 会话共 `-vnc :0` 互杀,console gate 临时 sed `-vnc :5`,跑完 `git checkout`。
- **sti-in-syscall #DF**(老 GOTCHA):批2 输入 ISR 只记账+唤醒,EOI 后再调度,绝不 inline schedule(同 sys-ping / PIT 重入 #DF / NVMe ISR 教训)。

## 协同

- 接 F13-B 收官(2026-07-06 合 main PR#70):IST 根治 + 花屏根治 + 桌面图标。Cinux-GUI origin/main `0b0c135`。
- 接 PTY 批2(Shell icon `on_activate` → `fork+execve /bin/sh`):本弧批3 用户态 host 自然落地。
- memory `gui-userspace-handoff` 是本弧的交接源。
