# CinuxOS — 当前焦点（批级进度）

> Tier 3（批级，易变）。单一事实源（批级）。全树见 `ROADMAP.md`，铁律见 `DIRECTIVES.md`。

## 🔄 F-USABILITY — 立项 2026-07-02（分支 `feat/f-usability`，从干净 main `6ba4a88`）

> 横切弧（与 F-VERIFY / F-INFRA / F-ECO 同档）。**目标：证明 CinuxOS 真能跑真实 Linux 用户态**——从「手搓 rootfs 夹具」升级到「Buildroot 工业级 rootfs + 进 shell 跑真实工具 + gcc/g++ 冒烟」。用户决策（2026-07-02）：① Buildroot 只当 rootfs 骨架打包器（external toolchain + busybox + musl），**GCC 全拷贝不编译**（`tools/gcc-toolchain/extract.sh` 已落地 PR#62）；② CI = PR 内编 + 强缓存（key = defconfig + overlay hash）；③ 手搓 `tools/musl/` + `create_ext2_disk.sh` **并存**留快反馈；④ 只动 ext2 disk（真 rootfs），ext4/ahci 测试夹具 + `build_image.sh` 启动盘不动。

> **可行性已实证**：99 syscall 覆盖 busybox rootfs 硬依赖 56/60，仅缺 `dup3/pipe2/recvmsg/sendmsg`（程序兜底）；**wait4 实际在 slot 61**（注册名 `SYS_waitpid`，handler 吃 4 实参，musl `waitpid` 传 `rusage=NULL` 兼容——之前 grep `SYS_wait4` 漏判）；busybox init PID1（F-ECO B3b）+ cc1→as→ld→./hello 全自举闭环（F12-M2 批4）均已跑通。双 libc 共存（musl 静态 busybox + glibc 动态 cc1）已 work，不用二选一。

> **2026-07-02 接管阻塞修复**：Buildroot 动态 busybox base rootfs 已在 build-console 进入 `/ #`。根因不是 PIE 整体不支持，而是低地址 `MAP_FIXED`、`PROT_NONE` fault 权限、Linux fd 0/1/2 分配/stdin 安装三组语义缺口；同步把 kernel stack 固定 8 KiB 并加 static_assert。验证 run-kernel-test-all 两腿 `1101/0` + host `69/69`。详见 [note](../notes/2026-07-02-f-usability-buildroot-busybox-fix.md)。

> **2026-07-03 production #DF + usability gate 收敛**：修 PIT/LAPIC timer IRQ inline schedule 重入窗口（tick 只记账+timer_queue wake，不在 hard IRQ 内 `schedule()`；timer EOI 移到 tick 后；`net_poll` 改低优先级合作式 yield），`run-buildroot-usability` 已闭环 PASS 并经 `cinux-exit 0` 退出；同步 inittab 用 `/bin/sh` 跑脚本、脚本用绝对 applet 路径。验证：production gate PASS + run-kernel-test-all 两腿 `1101/0` + full build + host `69/69`。 caveat：脚本里的 `PASS pipe` 暂为 console write smoke，真实 `echo | cat` 留 pipe EOF/BusyBox ash follow-up。详见 [note](../notes/2026-07-03-f-usability-pit-redf.md)。

> **2026-07-03 批3 gcc driver 单命令闭环**：`gcc -fno-pie -no-pie /hello.c -o /tmp/a.out && /tmp/a.out` 已在 Buildroot gcc rootfs 内跑通，输出 `Hello from GCC!` 并通过 `[usability] PASS gcc-compile-run`。修复横跨 forced SIGSEGV、`CLONE_VFORK` parent block + child exec detach、tmpfs truncate/create mode/chmod、file-backed VMA backing+offset 保真、`waitpid(-1)` 返回实际 child pid，以及 Arch GCC 的 `liblto_plugin.so`/`*_asneeded` staging。验证：`run-buildroot-usability` PASS + `run-kernel-test-all` 两腿 `1108/0`。详见 [note](../notes/2026-07-03-f-usability-b3-thread-gcc.md)。

> **范围栅栏**：Buildroot 走 external toolchain（不 internal 编 GCC，那个 CI 顶不住）；target gcc 进 rootfs 一律走 `extract.sh` 拷 host 闭包（as/ld/cc1 + glibc runtime + crt + libgcc + `gcc -H` 头文件闭包），**绝非 Buildroot 编 target gcc**；profile 是 job 属性不进 build_type×sanitizer 矩阵。

### 设计（三层分离，对齐 Buildroot/Yocto image-builder 范式）
```
Buildroot 产出(busybox+musl+目录骨架) ──┐
                                        ├─→ assemble staging ─→ mkfs ─→ rootfs.img
rootfs/overlay/(CinuxOS 专有:inittab    ─┤   (按 profile)
  + 可用性测试脚本 + smoke 源码)         │
extract.sh 产出(GCC 工具链闭包, 批3+)  ──┘
```
- `rootfs/buildroot/`：Buildroot defconfig（按 profile：base / gcc / g++）
- `rootfs/overlay/`：checkin 的静态树（定制 inittab、`etc/cinux-usability-test.sh`、`smoke/hello.c`）
- `scripts/assemble_rootfs.sh <profile> <staging>`：合成 staging 目录
- `scripts/pack_rootfs.sh <staging> <img> [fstype]`：纯打包（`mkfs.ext2 -d` / mkfs.ext4 / squashfs），零内容
- CMake 只消费 `rootfs.ext2` 一个文件，不知它怎么来 → 解耦

### 批表
| 批 | 阶段 | 范围 | 状态 | 验证 |
|----|------|------|------|------|
| 0 | 立项 | docs（本段）+ ROADMAP F-USABILITY + todo `f-usability/README.md` | ✅ `e12a386` | docs-only |
| 1 | 0+1 验证+接管 | Buildroot base defconfig（external+busybox+musl）；`create_ext2_disk.sh` 手搓并存；本地 boot 到 ash；overlay 合并 | ✅ `21b1d6a` | build-console boot 动态 busybox ash `/ #` + 两 leg 1101/0 + host 69/69 |
| 2 | 2 可用性测试 | overlay 内 `cinux-usability-test.sh`（ls/cat/uname/mkdir+rm/管道/fork-exec）；CI `buildroot-usability` job + `run-buildroot-usability` target（isa-debug-exit gate） | ✅ `33f187b`+`1e08083` | run-buildroot-usability 闭环 PASS + cinux-exit 0 SUCCESS + 两 leg 1101/0 + host 69/69 |
| 3 | 3 gcc 冒烟 | gcc driver 单命令跑通（codex `ec6d4b0` 5 根因：CLONE_VFORK/forced SIGSEGV/VMA backing-offset/tmpfs O_CREAT mode/waitpid reaped_pid）+ force_sig SMP follow-up（`fb42a1d` per-task sig_forced）+ assemble 固化（`43ad667`）+ worktree 合并（`5deb904`等+`072a588`）+ CI gcc-smoke job（`f9e35cc`，待 push 验） | ✅ 本地 | `gcc -fno-pie -no-pie /hello.c` 输出 Hello from GCC! + gate PASS；两 leg 1108/0 + host 69/0；CI gate 待 push |
| 4a | 4 g++ 冒烟（单线程） | 扩展 extract.sh（cc1plus+libstdc+++C++ headers+libm）；`g++ hello.cpp`；STL+异常试金石；**7 bug 修复链**（do_openat follow symlink / waitpid status 编码 / kprintf %.*s / extract libm self-loop / ProcFs+DevFs lookup_child / assemble 192MB+usr/lib64 / bb 断言解码）；详见 [note](../notes/2026-07-04-f-usability-b4a-gpp-cpp.md) | ✅ 本地 | g++ 闭环 PASS（`Hello from G++!`+`exception: caught`）；两 leg 1108/0 + bb 14/14 + host 69/69；ACT 撞 nektos/act issue 107（container cwd，非脚本），真 CI 待 push |
| 4b | 4 pthread 试金石 | gettid/set_robust_list/sched_getaffinity stub + clone(CLONE_THREAD) 真跑 + pthread create/join/mutex/cond | ⏳ | pthread 闭环 |
| 5 | 5 扩包 | util-linux / coreutils 完整版 / dropbear sshd（按需） | ⏳ | 按需 |

### 风险 / 陷阱
- **CI 编译时间**：Buildroot external+busybox+musl 首次 5-10min（base profile）；gcc 闭包拷贝秒级。`build-rootfs` job 长 timeout（base 20min），强缓存 key=defconfig+overlay hash，命中秒过。批3 gcc-smoke 的 extract.sh 闭包拷贝秒级、不缓存也行。
- **镜像体积**：base profile ~8MB ext2；gcc profile（+47MB cc1 + glibc runtime + 头文件闭包）需扩容，g++ 更甚。批3 起按需扩 IMAGE_SIZE 或切 squashfs（只读压缩，headers 文本多压缩比好；可写区走 /tmp tmpfs）。
- **target vs cross 工具链（头号坑）**：进 rootfs 的 GCC 必须是 **native**（在 CinuxOS 跑、产 CinuxOS 二进制）。extract.sh 拷的是 host 的 cc1/as/ld——它们是 glibc 动态的 host 二进制，靠一起拷进来的 glibc `.so` + `/lib64/ld-linux` 在 CinuxOS 跑（已验证）。**绝不能**塞 Buildroot 的 cross compiler（host 程序，CinuxOS 跑不了，一跑 SIGSEGV 且难分辨是内核还是工具链塞错）。
- **wait4 命名陷阱**：slot 61 注册名是 `SYS_waitpid` 但就是 Linux `__NR_wait4`（handler 4 实参）。别再 grep `SYS_wait4` 漏判。
- **Buildroot 本机依赖**：make/gcc/g++/bison/flex ✅；rsync/cpio/ncurses-dev/libssl-dev 阶段0 前置核对。
- **批3 follow-up（登记，留后续）**：①unhandled syscall stub 降噪（glibc probe：318 getcpu / 435 clone3 / 273 rseq / 334 / 302，现走 ENOSYS fallback 非致命，补 stub 改善 CI log 可读性）；②既有信号 SMP 债（sig_pending/sig_blocked 普通 uint64 无原子，SMP 投递竞态；批3a-2 只修 force_sig 新债 per-task sig_forced，既有债留独立 follow-up）；③gcc driver 残留（collect2 fork-exec + pipe 连 cc1/as/ld，B4-C2+批3 修了大头，残留 pipe EOF 对齐 BusyBox ash）；④CI gcc-smoke 真验（批3b-2 写完结构，push 后验 buildroot 下载/编译 + assemble + gate）；⑤**busybox echo/cat ✅批1+批3 根治（`6d21066`+`8bf1b6d`）**——深挖超 fd 污染，三真 bug：①File 独占语义→double-free，**批1 FileRef RAII**（File 加 `refcount` public 仿 inode + `FileRef` 值类型 copy +ref/析构 -ref 到 0 delete，无 friend，`FDTable` 持 `FileRef`；inode ref 留 FDTable 避栈 Inode 析构 UAF）；②`sys_pipe` 失败路径漏回滚 `do_pipe_kernel` alloc 的 2 fd（生产也漏），加 `tbl.close` 回滚；③`do_read fd=0` legacy `console_tty().read` NotNull-panic（测试 ring0 `current()==null`，short-circuit -EBADF）+ `test_socket_*` 漏 close。治标 close（`7e5f3b6` smoke entry）保留（fd 0/1/2 污染 echo fflush 仍需它清）。两 leg 1108/0 + busybox 14/14 + host 69/69。⭐"盾"现象：批1 绿部分因 pipe 漏 close 泄漏 fd 0/1 当盾（避 test do_open 拿 fd 0），修泄漏暴露 do_read 地雷。**批2（dup/fork/clone 共享 File 对齐 Linux offset，memory `f-eco-b3-b4`）+ reserve/legacy 根治（删治标 close）留 follow-up**，详见 [note](../notes/2026-07-03-f-usability-fileref-fd-pollution.md) + plan `~/.claude/plans/sunny-hopping-journal.md`。
- **批4a follow-up（登记，留后续）**：①ext4 rootfs（ext2 驱动只读 ext4 extent depth-0 leaf，**extent 写是 F6-M5 follow-up**；可写 rootfs 必须留 ext2 `mkfs.ext2 -O none`，批4a 暂扩 IMAGE_SIZE 192MB）；②do_open_kernel follow（sys_open 旧路径 ring0 测试专用，不 follow symlink；生产 sys_open/sys_openat → do_openat 已 follow，一致性非 gate 阻塞）；③sys_readlink 对 regular 文件应返 EINVAL（现读 i_block 当 long symlink，次要）；④PIE（**批1 kernel ✅ 2026-07-05**：execve 检测 ET_DYN 选 `USER_EXEC_BASE(0x20000000)+aslr_exec_base_offset`，`entry_va`/`AT_ENTRY` 加 base 偏移；`build-hello-dyn.sh` 去 `-no-pie` 产真 PIE。⭐踩坑：brk heap 的 `USER_BRK_MAX(240MB)` 是绝对上限(为 ldso 让路)，PIE main 在 512MB 撞穿→heap VMA 倒置→execve -12；修 `brk_max` 跟随 image(ET_EXEC→`USER_BRK_MAX` / PIE→`USER_MMAP_BASE`)。两 leg `/hello`(ET_EXEC)20/20 + `/hello-dyn`(PIE)5/5 PASS + ASLR base 10 次全不同。**批2 toolchain ✅**：extract.sh/usability 去 `-fno-pie`，gcc/g++ 默认 PIE，run-buildroot-usability PASS（gcc a.out base=0x2024D000 / g++ cpp.out base=0x209E4000，ASLR 生效）。⭐踩坑：GUI=ON 让 usability gate 卡(kernel_init 走 GUI desktop 不跑 busybox init PID1)，gate 须 GUI=OFF。详见 [note](../notes/2026-07-05-pie-b1.md)/[b2](../notes/2026-07-05-pie-b2.md)）；⑤ACT nektos/act issue 107（container cwd `/tmp/...` no such file + composite action path 空，act 0.2.89 + Docker Desktop；升 act 或换 `--bind` 配置，**非脚本**——`act_ci_job.sh` 固化 docker/image/workdir/git-init/prepare 检查全过）；⑥批4b pthread（gettid/set_robust_list/sched_getaffinity stub + clone CLONE_THREAD 真跑，机制有未压测）。详见 [note](../notes/2026-07-04-f-usability-b4a-gpp-cpp.md)。
- **手搓并存纪律**：`tools/musl/` + `create_ext2_disk.sh` 保留作快反馈/调试载体；两条 rootfs 走不同 CMake option/profile，Buildroot 出问题时能定位是内核还是 rootfs 的锅。

## 🔄 F5-M3 NVMe（PCIe SSD）— 立项 2026-07-05（分支 `feat/f5-nvme-virtio`，从干净 main `bef86b0`）

> Feature 域 F5 第三里程碑。NVMe PCIe SSD 驱动，实现 IBlockDevice，目标给 gcc 编译 I/O 提速（实测 KVM gcc 编译 I/O bound 87%，per-page AHCI ~2.4ms vs NVMe ~50μs 理论 ~40×；QEMU 仿真打折扣，真机才接近）。接 F5-M1 AHCI DMA ✅（IBlockDevice + DmaPool 参考）、F5-M5 xHCI ✅（MSI-X 参考）、F5 PCI 框架 ✅。**用户决策（2026-07-05）**：① 接入策略 = **并存验证**（NVMe 独立第二盘，生产 rootfs 仍走 AHCI，perf 批把 gcc rootfs 放 NVMe 盘对比）；② 范围 = 完整 5 批（批0→5）。VirtIO（M2）是下一弧，本弧不碰。

> **接入缝（零改 ext2）**：`Ext2` 持裸 `IBlockDevice*`（[ext2.hpp:55](../../kernel/fs/ext2/ext2.hpp#L55) `explicit Ext2(IBlockDevice* dev)`），NVMe 再造一个 `NvmeBlockDevice` 传给 Ext2 构造，ext2/PageCache 零改。**MMIO 槽位（KMEM_MMIO 2MB 窗）**：现有 AHCI@+0 / LAPIC@+0x10000 / IOAPIC@+0x11000 / xHCI@+0x20000 / MSI-X@+0x40000 / e1000@+0x50000 / HPET@+0x60000 → NVMe BAR0 @+0x70000、NVMe MSI-X Table @+0x74000 / PBA @+0x75000。

### 批表
| 批 | 范围 | 状态 | 验证 |
|----|------|------|------|
| 0 | 立项 docs（本段）+ ROADMAP F5-M3 ⏳→🔄 + todo `02-nvme.md` 范围栅栏 | 🔄 | docs-only |
| 1 | PCI 枚举 NVMe（`find_nvme` class=0x01/subclass=0x08）+ BAR0 映射 @+0x70000 + QEMU 加 `-device nvme` 独立盘 + 机制测读 CAP/VS 证映射真生效 | ✅ `1b7fc5a`+nvme | 两 leg 886/0 + 机制测 MQES=2047 VS=1.4 |
| 2a | Controller enable（CC.EN↔CSTS.RDY 握手）+ Admin SQ/CQ 配置（AQA/ASQ/ACQ + 4KB DmaBuffer） | ✅ | 两 leg 886/0 + CSTS.RDY=1 |
| 2b | doorbell + Identify Controller（Admin SQ 提交 + 轮询 CQ）+ CAP.DSTRD 解码确认 | ✅ `a471f22`+2b | 两 leg 886/0 + Identify VID=0x1b36 SSVID=0x1af4 MN=QEMU NVMe Ctrl |
| 3 | MSI-X 多实例：`MsixController::init` 加 table_virt/pba_virt 默认参数（xHCI 用默认 +0x40000 不变；NVMe 传 +0x74000/+0x75000）+ NVMe `init_msi_x`（entry 0 -> vector 0x41）| ✅ | 全量编绿（xHCI 不破）+ 两 leg 886/0 + init_msi_x 真验（msix_table[0] 编程）；⭐dev_.bar[0] stale 治（read_bars 在 self-assign 前→MSI-X Table phys 算低内存→写低 RAM 破坏 0x1234 区→clone acquire #PF；修 init 更新 dev_.bar[0]）|
| 4a | Identify Namespace（CNS=0x00）+ admin_submit helper + Create IO Cq/SQ + ISR（IDT[0x41]）+ enable MSI-X | ✅ | 两 leg 1844/0 + Create IO Cq/SQ status=0；⭐根因 CC 字段位错（IOSQES=[19:16]/IOCQES=[23:20]，非 [29:24]/[21:16]；0x8205=MAX_QSIZE_EXCEEDED SC=2 非 IV SC=8，IV 是误诊）+ NVMe MSI 污染 LAPIC（test kernel 没 switch_to_apic→0x41 占 ISR 阻塞 e1000 timer；修 init_msi_x mask_all）。note `2026-07-06-f5-m3-b4a-create-io-queue.md` |
| 4b | NVM Read/Write（单页 PRP1，opcode 0x02/0x01）+ io_submit + round-trip 机制测 | ✅ | 两 leg 1844/0 + Read/Write round-trip 512 bytes OK；note `2026-07-06-f5-m3-b4b-nvm-read-write.md`。PRP list（>1 页）留批4c |
| 4c | NvmeBlockDevice（IBlockDevice 适配，抄 AHCIBlockDevice）+ main.cpp Step 21a 注册（并存）+ test round-trip | ✅ | 两 leg 1844/0 + NvmeBlockDevice round-trip OK；production boot 待用户 GUI 验。note `2026-07-06-f5-m3-b4c-nvme-block-device.md`。ISR install（批4a）+ mask_all polling；unmask 真异步 IRQ 留 follow-up |
| 5 | perf 量收益：NVMe 盘跑 gcc/g++ 编译对比 AHCI 基线 [MEM] I/O + NVMe 默认 boot disk + 收官 | ✅ | NVMe 9365ms vs AHCI 10763ms（−13% I/O，QEMU 仿真折扣理论 40×）；NVMe 默认 rootfs（`run` target）+ AHCI legacy fallback（init.cpp runtime 选盘）；两 gate PASS + TCG 回归 922/0。note `2026-07-06-f5-m3-b5-perf-nvme-vs-ahci.md` |

### 风险 / 陷阱
- **⭐ SeaBIOS 不配 QEMU nvme BAR0（批1 踩坑，假绿）**：`dev.bar[0]=0x0`（SeaBIOS 配 AHCI/e1000 但跳过 QEMU nvme），读 CAP/VS 映射到 phys 0x0（低 RAM）返垃圾值恰好满足 "> 0" 断言 = **假绿**（MQES=65363/VS=0xf000e2c3）。修：NVMe init 加 BAR self-assign（probe size=16384 + 固定槽 `0xfeb40000`）+ **64-bit BAR 必须清 BAR1**（rb1=0 才使地址=0xfeb40000 < 4GB），读到真 MQES=2047/VS=1.4（NVMe 1.4）。教训：机制测断言要能区分真值 vs 垃圾（MQES 合理范围 + MJR>=1，不只 >0）。通用 PCI BAR 分配器留 PCI 框架 follow-up（批1 NVMe 内 self-assign）。
- **⭐ CAP.DSTRD 解码（批2b 修正）**：NVMe 1.4 CAP.DSTRD = bits[31:28]（4 位），doorbell stride = 4 << DSTRD。批1/2a 误用 `(cap_lo>>24)&0xF`（bits[27:24] = TO 高半字节）显示 DSTRD=15（误导，真值 0）；批2b 改 `(cap_lo>>28)&0xF` → DSTRD=0/stride=4，Admin SQ tail @ BAR0+0x1000、CQ head @ BAR0+0x1004。
- **sti-in-syscall #DF（致命 GOTCHA）**：NVMe 中断 ISR 绝不 inline schedule（同 sys-ping-df-sti-in-syscall / PIT IRQ 重入 #DF，批3 PIT/LAPIC timer inline schedule 重入窗口已修过）。中断只记账+唤醒，EOI 后再调度。
- **MsixController 硬编码 +0x40000**（[msix_controller.cpp:30](../../kernel/drivers/pci/msix_controller.cpp#L30) `kMsixTableVirt`）：xHCI 专用，NVMe 要第二实例须给 `MsixController::init` 加 `table_virt/pba_virt` 参数（或构造注入）。**改公共接口，push 前全量编**（CI 盲区：run-kernel-test 不编 test/unit host 单测）。
- **PRP SGL**：NVMe 用 PRP（Physical Region Page，4KB 对齐物理连续），第一 PRP + PRP list 链（>2 页传输）。复用 DmaPool（参考 [AHCIBlockDevice](../../kernel/drivers/ahci/ahci_block_device.hpp) 持单 DmaBuffer 模式）。
- **QEMU nvme 仿真 overhead**：理论 per-page ~50μs ≈ 40× AHCI，但 QEMU 仿真打折扣；真机才接近。批5 诚实标注，不包装成真硬件收益。
- **smoke 默认 ON 挂死**：本地 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF`（同 F10/F5-M4 惯例）。
- **VNC 避让**：多 AI 会话共 `-vnc :0` 互杀 QEMU，验证临时 sed `-vnc :5`，跑完 `git checkout`（同 verify-vnc-port-collision-multi-session）。
- **范围栅栏（不投机）**：批1-4 不动 AHCI 链（并存）；NVMe 不进 production Ext2 装配（批4 只注册独立盘 + 机制测，生产切换留 follow-up）；VirtIO（M2）下一弧，不碰。

## 🔄 F5-M2/M7 VirtIO 块设备 + 网卡 — 立项 2026-07-06(分支 `feat/f5-virtio-blk-net`,从干净 main `17798fa`)

> F5 第三大存储弧 + 网卡收尾(NVMe 弧之后的下一弧,如上 NVMe 范围栅栏所述)。VirtIO 传输层(PCI modern capability + split virtqueue)新写,块设备 + 网卡两设备共享;复用 NVMe 弧趟过的 PCI/MSI-X 多实例/DmaPool/IBlockDevice + e1000 的 NetDevice/协议栈范式。**用户决策 + 约束(2026-07-06)**:① virtqueue 用 polling 跑稳机制测后,**紧接着直接上真异步 MSI-X 中断**提升性能——不要像 NVMe 弧停在「ISR install + mask_all polling」把真中断留 follow-up;② 严防 -smp 竞态(NVMe `749e7db` NvmeBlockDevice 单 DmaBuffer 无锁在 -smp 2 崩的教训——共享状态从一开始就 Spinlock)。

> **接入缝(零改 Ext2/PageCache/协议栈)**:同 NVMe,`Ext2` 持裸 `IBlockDevice*`,`VirtIOBlock` 再造一个 IBlockDevice 传给 Ext2 构造;virtio-net 走 `NetDevice` 接口抄 e1000,挂 net_init 注册,L3/L4 协议层零改动。

### 批表
| 批 | 范围 | 状态 | 验证 |
|----|------|------|------|
| 0 | 立项 docs(ROADMAP/PLAN/todo 01+06)+ `CINUX_VIRTIO` option + QEMU 加 virtio-blk-pci + PCI `find_virtio_block`/`find_virtio_net` | 🔄 | docs-only + 全量编 + 两 leg 零回归 |
| 1 | VirtIO 传输层(`virtio.hpp` PCI cap 遍历 + feature 协商 + status 机)+ `virtqueue.hpp/cpp`(split queue desc/avail/used + submit/wait/notify)+ polling 机制测(queue round-trip) | ⏳ | 两 leg + cap offset 回读 + queue 提交回收 |
| 2 | virtio-blk 驱动(read/write/flush + `VirtIOBlock : IBlockDevice` 抄 NvmeBlockDevice + main.cpp Step 注册)polling | ⏳ | 两 leg + read/write round-trip byte-compare + Ext2 mount |
| 3 | ⭐**virtio-blk 真中断 + SMP**:MSI-X unmask(vector 0x42)+ ISR flag/wake(**不 inline schedule**,避 #DF)+ Spinlock 从第一天护 DmaBuffer/virtqueue + -smp 2 压测 | ⏳ | 两 leg(单核 + **-smp 2**)+ 并发 read_blocks 不出 garbage |
| 4 | virtio-net 驱动(RX/TX virtqueue + `VirtIONetDevice : NetDevice` 抄 e1000 + net_init 注册)polling + SLIRP ping smoke。先 fixed header,mergeable buffer 留续 | ⏳ | 两 leg + ping 10.0.2.2 + loopback echo |
| 5 | ⭐**virtio-net 真中断(3 向量 0x43-0x45)+ SMP + perf + 收官**:MSI-X RX/TX/config + ISR + Spinlock RX/TX buffer + -smp 2 验 + perf virtio-blk vs NVMe vs AHCI + note + ROADMAP F5-M2/M7 ✅ | ⏳ | 两 leg(单核 + **-smp 2**)+ ping + perf 数据 |

### 风险 / 陷阱
- **⭐ polling→真中断(用户约束,核心)**:virtqueue polling 机制测稳后**紧接着**上真异步 MSI-X(NVMe 弧停在 ISR install + mask_all polling 留 follow-up,本弧不留)。ISR 只 flag + wake wait_queue,**绝不 inline schedule**(避 sti-in-syscall #DF,同 sys-ping-df / PIT IRQ 重入 #DF);EOI 后再调度。
- **⭐ -smp 章态(NVMe `749e7db` 教训)**:单 DmaBuffer + virtqueue 环 + per-device RX/TX buffer 从第一行就 Spinlock,别等 -smp 2 崩了补(-smp 2 多核并发 read_blocks 覆盖单 DmaBuffer → garbage → ext2 坏)。`run-kernel-test-all` 两 leg 本含 -smp 2(F-VERIFY 基建)。
- **VirtIO modern cap 遍历**:4 组寄存器(common_cfg(cap_id=0x09 子类型 1)/ notify(子类型 2)/ isr(子类型 3)/ device_cfg(子类型 4))offset 要对,漏一个协商不过。批1 机制测先证 cap offset 回读正确(对标 NVMe 批1 假绿——MQES 要能区分真值 vs 垃圾)。
- **MSI 向量避撞**:0x42(blk)、0x43-0x45(net RX/TX/config),严防 0x41(NVMe,曾污染 LAPIC 占 ISR 阻塞 e1000 timer)+ 0x40(xHCI)+ 0x30(LAPIC timer)。
- **transitional vs modern device ID**:QEMU `-device virtio-blk-pci` 默认 transitional(PCI device_id 读 0x1001)但走 modern capability transport。`find_virtio_block` 匹配 vendor 0x1AF4 + device ∈ {0x1001 legacy, 0x1042 modern};transport 一律 modern(legacy transport 不做)。
- **MsixController 第 3/4 实例**:NVMe 弧已加 `table_virt/pba_virt` 参数,xHCI @+0x40000 / NVMe @+0x74000;virtio-blk MSI-X Table @+0x84000、virtio-net @+0x86000(避撞)。
- **smoke 默认 ON 挂死**:本地 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF`(同 F10/NVMe 惯例)。
- **VNC 避让**:多 AI 会话共 -vnc :0 互杀 QEMU,验证临时 sed -vnc :5,跑完 git checkout(同 verify-vnc-port-collision)。
- **范围栅栏(不投机)**:批1-3 只 virtio-blk(不碰 AHCI/NVMe 链,并存独立盘);批4-5 virtio-net(不拆 e1000,独立 SLIRP netdev);packed virtqueue / scatter-gather 多链 / mergeable buffer 留续。

## ✅ GCC 自举主线 — 阶段收尾 2026-07-05（批1 tmpfs ＋ shm ＋ 批2 mount/tmp ＋ 批3a sys_access ＋ 批3b busybox init PID1 → 批4-a as+ld 自举 ＋ 批4-C2 cc1→as→ld→./hello 全闭环，合 main PR#61/PR#62/PR#66）

> **2026-07-05 GCC 线阶段收尾**：CinuxOS 上 gcc/g++ 编译 + 跑用户程序全链路绿。达成：① F12-M2 批4-C2 cc1→as→ld→./hello 自举闭环；② F-USABILITY 批3 gcc driver 单命令 ✅ + 批4a g++ STL+异常 ✅（PR#63/64/65）；③ busybox init PID1（批3b）；④ C 重构治 lto_plugin corrupt（PR#65，refcount/pte_count 分离）；⑤ perf 弧 ext2 agg 降 gcc 编译 I/O 25%（PR#66）；⑥ 默认 PIE gcc/g++ + main-base ASLR（PR#66 PIE B1+B2）；⑦ syscall 补全 gcc/g++ unhandled 90→0（PR#66）。CI 全绿。详见收官 [note](../notes/2026-07-05-gcc-line-finale.md)。

> **open follow-up（非 gcc/g++ 编译必需，登记留后续）**：① pthread（F-USABILITY 批4b，gettid/sched_getaffinity + clone CLONE_THREAD 真跑）；② robust futex 真清理（set_robust_list 现返 0 假成功）；③ sendfile 真实现；④ 199 fremovexattr + xattr 系列；⑤ interpreter ASLR（ldso 固定 USER_INTERP_BASE）；⑥ 静态 PIE；⑦ ext4 rootfs（F6-M5）；⑧ F12-M3 self-hosting（编 CinuxOS 自己，远期大弧）。

> **批4-a 收敛（2026-07-02 会话三）**：前次 “as+ld ✅” 曾误判（`./hello` 实际是盘上预放 musl hello），随后 ld 自举拆出并修完 6 个屏蔽层：① file-backed MAP_PRIVATE writable demand-page 缺 CoW（as 重定位污染 libbfd page cache）✅；② file-backed page cache PTE 缺 mapcount ref（进程退出 free 共享 cache 页）✅；③ ext2 unlink indirect 释放中 `free_block` 覆盖单 `block_buf_` + double-indirect 漏释放 ✅；④ ext2 write 直写磁盘绕过 PageCache，as 写 `/hello.o` 后 ld mmap/read 命中 stale cached page ✅（`PageCache::invalidate_range`）；⑤ uaccess extable 早于懒缺页，`read()` 大 buffer 跨未触达 malloc/mmap 页被误判 `-EFAULT` ✅（有效 VMA not-present faults 先 demand-page 再恢复 `rep movsb`）；⑥ toolchain staging 漏 link-time `libc.so/libc_nonshared.a/libc.a` ✅。验证：手动 TCG 同一测试镜像 `1098 passed,0 failed` + `[B4-B3] ld /hello.o -o /hello PASS` + `Hello from GCC!` + `./hello PASS`。

> **批4-C1 cc1 --version ✅（2026-07-02 会话四）**：cc1（~47MB GCC C 前端,9 DT_NEEDED：libisl/libmpc/libmpfr/libgmp/libm + as/ld 闭包的 libz/libzstd/libc/ldso）装盘 + `cc1 --version` 跑通。「最不可控」一关首战告捷 —— bug4 的 page-cache/uaccess/ext2 修复扛住 cc1 更重 ldso bring-up + TLS + glibc -O2 构造函数,未暴露新内核 bug。改 `extract.sh`(stage cc1 + ldd 闭包)+ `main_test.cpp`(B4-C1 smoke)。commit `54e61ca`。

> **批4-C2 cc1 编译→as→ld→./hello 全自举闭环 ✅（2026-07-02 会话五）**：CinuxOS 上 `cc1 /hello.c -o /hello.s` → `as` → `ld` → `./hello` 输出 **"Hello from GCC!"**(CinuxOS 自己编自己跑的 C 程序)。修 4 个独立 bug:① **cc1 缺 `stdc-predef.h`** —— GCC 隐式 preinclude,`gcc -H` 不列,`extract.sh` 显式 stage hello.c 的 ~25 文件 include 闭包(gcc -H 动态算,~200KB,非全量 249MB /usr/include)。② **`O_TRUNC` 完全没实现** —— cc1 覆写 /hello.o 留旧尾巴(as 读到 `gbits` "no such instruction");加 `InodeOps::truncate`(默认 NotImplemented,ext2 实现:set i_size+persist+VMS size,块释放留 follow-up)+ `do_openat_kernel` O_TRUNC(0x200)调用。③ **Bug A — 内核对 user #PF panic**(该 SIGSEGV):handle_pf 对 user 写只读页等不可解 fault 直接 `panic()`,Linux 投 SIGSEGV 进程;加 user-fault SIGSEGV 路径(kernel fault 才 panic)—— **内核兜底,不再因用户态错误崩**。④ **Bug B — VMA backing inode 悬垂指针(根因)**:VMA 持裸 `Inode*` 无引用计数,ext2 inode_cache_ 驱逐复用 slot 后 VMA 服务到错的文件(ld 跑 cc1 产的 .o 时,某库 .text VMA backing 指向被释放复用的 libc.so 脚本 inode → page cache 返回 EOF 零页 → ld 执行 `00 00`=`add %al,(%rax)` → 写保护 #PF)。修:sys_mmap 设 backing 时 `inode_ref`;vma.cpp 删节点(clear/merge/remove)+ split 复制 backing 时 `inode_ref/unref`;**ext2 驱逐跳过 refcount>0 的 slot**(pinned by fd/VMA)。复用既有 DEBT-023 `Inode::refcount`。两 leg run-kernel-test-all EXIT=0(cc1/as/ld/./hello 各 2/2)+ host 69/0 + 0 panic。⭐**教训**:VMA/File 持 inode 必须引用计数,否则 slab 复用导致 use-after-free(同 page_cache.hpp 注释警告的 key-by-pointer 陷阱,VMA backing 同样适用)。

> **进度**：批1 tmpfs `6656096` ✅ ＋ shm merge `2a86b13` ✅ ＋ 批2 mount/umount2+/tmp `b078fef` ✅ ＋ 批3a sys_access `25271b3` ✅。目标 `gcc hello.c -o hello && ./hello`。4 批串行：批1 ✅ → 批2 ✅ → **批3 sys_access✅ + init PID1(focus)** → 批4 GCC 工具链装盘 + gcc hello.c。
> **批3a（✅ `25271b3`）**：sys_access(21) 文件权限检查。`access_granted()` 标准 Unix 判定(owner/group/other 三元 + root 旁路:R/W 直通、X 需 exec 位)→ 可复用给 F6 check_permission / login。机制测 2:root 旁路 R/W + 0644 文件 X_OK 拒(-EACCES)+ 缺路径→ENOENT + 坏 mode→EINVAL。
> **批3b（✅ `460b474` + 子模块 Cinux-Base `75df90f`，2026-07-02）**：busybox init 作 PID1。**生产冒烟(build-console 非 GUI)确认** busybox init PID1 + sysinit echo + respawn /bin/sh + ash 提示符(`~ #`) + PID1 reap child（批4 GCC 孤儿基础）。**4 处改动 + 扩展**：① `init.cpp` 入口 alloc PID1（kernel_init 模型，kthread pid=0 不碰 g_pid_alloc，**推翻旧交接"重排 start_poll_driver"误判**）+ usb::init 挪前；② `shell_launch.cpp` 直 execve /sbin/init 不 fork（argv{"init"}）；③ devfs /dev/console 代理 console_tty（ConsoleInput 注入 read+ioctl 保 host-testable）+ console_tty_ioctl 共享（sys_ioctl 复用）+ **Cinux-Base Error 加 Fault(EFAULT)** 根治 copy 失败近似（原 InvalidArgument≈EINVAL 不准；console 有 EFAULT 测试闸）；④ 盘加 /etc/inittab + /sbin/init busybox hardlink。**扩展（生产冒烟暴露）**：补 busybox init 三件套 syscall——vfork(58)=fork / reboot(169)=-EPERM / rt_sigtimedwait(128)=-EAGAIN（驱动 respawn loop；pure block 死锁因 SIGCHLD 被 waitpid 消耗）+ signal_send 加 sigwait waiter 唤醒基建。验证：两 leg run-kernel-test-all 绿 + host 69/69 + EFAULT 测试绿。详见 [note](../notes/2026-07-02-gcc-b3b-busybox-init-pid1.md)。push/PR 归用户。
> **批4 GCC 工具链(glibc 动态)🔄**（分支 `feat/b4-gcc-toolchain` 从 main `0b82e1b`）：**用户决策校正 2026-07-02** — 动态 glibc（兼容最广）+ 不自编工具链（硬盘/必要性）+ 先 as+ld 链路再 cc1 + musl/glibc 共存（首选;内核 PT_INTERP 中立;回退条件=glibc 跑不通）。杠杆：PT_INTERP 是 libc 切换缝,内核动态加载机制复用（F10-M2 验 musl,本弧切 glibc）。**B4-a 第一刀 = as+ld 链路**（hello.s host 预编,省 249MB headers）。**B0 内核 glibc 兼容 4 处 ✅**：auxv +AT_PLATFORM("x86_64")/AT_HWCAP(CPUID.01H:EDX) + execve 扫 PT_GNU_STACK（栈默认 NX,glibc 期望）+ USER_BRK_MAX 64MB→240MB（留 ldso@256MB gap;动态增长留续）。host 69/69 + 两 leg ALL PASSED。vDSO 留 follow-up（glibc fallback syscall）。下：B1 host 提取 as/ld+glibc 运行时 → mkfs.ext2 -d 目录树装盘（debugfs 逐文件退役）。详见 plan `parsed-zooming-fog.md`。
> 验证(批1/2/3a):两 leg run-kernel-test-all 全绿(tmpfs 9×2 + mount 5×2 + access 2×2 + shm 6×2,busybox 14/14,零 FAIL)+ `make run` 冒烟 `[TMPFS] mounted at /tmp`。详见批1/批2 note。push/PR 归用户。

> 目标：`gcc hello.c -o hello && ./hello` 在 CinuxOS 跑通——从「能跑别人二进制」到「能编自己二进制」。ABI 底子已验（busybox -O2 14/14 试金石过）。4 批串行：批1 tmpfs ✅ → 批2 mount/umount2+挂 /tmp ✅ → 批3 sys_access+init PID1 → 批4 GCC 工具链装盘 + gcc hello.c。
> **批1（✅ `6656096`）**：TmpFs 内存型虚拟 FS（[tmpfs.hpp](../../kernel/fs/tmpfs/tmpfs.hpp) / [tmpfs.cpp](../../kernel/fs/tmpfs/tmpfs.cpp)）——第一个**运行时改写自身目录结构**的 FS（DevFS/ProcFS mount 时定死）。`TmpNode` 堆分配内嵌 Inode（`fs_private` 指回 node，延用 DevFs 招）+ 单向兄弟链表子节点（无上限）+ 可扩容堆缓冲（4KB 对齐增长 + gap 零填）+ 单 per-FS Spinlock。`TmpFileOps`(read/write/stat) + `TmpDirOps`(readdir/create/mkdir/unlink/stat)。lookup 多段 walk（仿 ext2，syscall `split_pathname` 拆 `sub/file` 故 tmpfs 须自 walk）。`is_page_cacheable` 默认 false → 内容直连 `ops->read/write`，不进磁盘 PageCache（已核实 sys_read/sys_write 路由）。
> **shm merge（✅ `2a86b13`）**：并入 F8-M4 SysV 共享内存（`feat/f8-m4-shm` 2 commit 保留无 squash，ort 自动合并两测试登记文件）。
> **批2（✅ `b078fef`）**：sys_mount(165)/umount2(166) + boot 自动挂 /tmp。fstype 工厂（tmpfs→堆 TmpFs，他→ENODEV）+ `do_mount/umount2_kernel` 内核变体（ring0 测直驱，sys_ 包 SMAP user-ptr）。**MountPoint +`owned` 字段** + `vfs_mount_add(,,owned=false)` 默认参 + `vfs_mount_remove` 变 **ownership-aware**（sys_mount 堆 FS umount 时 delete，boot 静态/测 mock 不释放）——现有 13 处 remove 全 owned=false，**零行为变**。`tmpfs::init()` boot hook（procfs::init 后挂 /tmp，静态 unowned）。
> **机制测**：批1 tmpfs 9（直驱 InodeOps）＋ 批2 mount 5（mount+resolve / mount→create-write-read / 未知 fstype→ENODEV / umount 缺失→ENOENT / remount-after-umount 验 owned 真释放）。
> **GOTCHA**：① 头 `class TmpNode;` vs cpp 匿名 ns `struct TmpNode`（-Wmismatched-tags）→ 定义放 cinux::fs 非匿名 ns；② ring0 测不能走 sys_ 的 user-ptr 路径（is_user_vaddr 拒内核地址）→ 直驱 do_；③ ownership-aware remove 零行为变靠 `owned` 默认 false（boot/测全 false，唯 sys_mount true）；④ 测试内核无 /tmp（跑 big_kernel_test 不走 init.cpp）→ 生产 /tmp 由 `make run` 验。⭐**VNC 避让**：多 AI 会话共用 `-vnc :0` 互杀 QEMU，验证切 `-vnc :5` 跑完还原。
> 验证：批1 两 leg 绿（TmpFs 9/9）；批2 **`make run` boot 冒烟 `[TMPFS] mounted at /tmp`** + 两 leg 绿（mount 5×2 + tmpfs 9×2 + shm 6×2，busybox 14/14，零 FAIL）。详见批1 [note](../notes/2026-07-01-f6-m4-b1-tmpfs.md) / 批2 [note](../notes/2026-07-01-f6-m1-b2-mount-tmpfs-boot.md)。push/PR 归用户。

## ✅ F6-M5 ext4 extents（读路径）— 收官 2026-07-01（分支 `feat/f6-m5-ext4`，worktree `.claude/worktrees/wt-ext4`，从干净 main `5c03f85`，两 leg 1066/0 + host 绿）

> Feature 域 F6 VFS 第五里程碑。ext2 驱动扩展支持 ext4 extent tree（depth-0 leaf）读：inode 置 `EXT4_EXTENTS_FL` 时 `i_block[0..14]` 是 extent tree 而非块指针。**新增 `ext2_extent.{hpp,cpp}`**（resolver 独立文件——`ext2_common.cpp` 已 483 行撞 500 软上限）：`extent_lookup_block` 解析 leaf extent 算逻辑→物理块映射 + `inode_read_block` 共用读侧解析。**接线**：`Ext2FileOps::read` 加 extent 分支；`lookup_in_dir`/`readdir` 改用 `inode_read_block`（**目录在 ext4 也是 extent-mapped**，否则 lookup 全 NotFound——本批头号坑）。**范围栅栏**：depth-0 leaf；depth>0 index / extent 写 / journal 留续。**测试**：独立 ext4 盘挂 AHCI port 2（`create_ext4_disk.sh`，`mkfs.ext4 -O extents,^64bit,^metadata_csum` 强制 32 字节 BGDT——驱动按 32 字节定步长不看 `s_desc_size`）+ `test_ext4_extents.cpp`（mount ext4 卷 / 大文件 1 MiB 全 pattern byte-exact / 跨块读 / 小文件）。详见 `document/notes/2026-07-01-f6-m5-ext4-extents.md`。

| 批 | 内容 | 状态 | 验证 |
|----|------|------|------|
| 1 | ext4 类型 + extent resolver + read/dir 接线 + ext4 镜像脚本/QEMU 接线 + 机制测 | ✅ | 两 leg 1066/0 + host 绿 |

## ✅ F-ECO 批8 小件（getgroups + setgroups）— 收官 2026-07-01（外包 worktree `feat/outsource-f-eco-b8-groups`，从集成线 `9208751`，cherry-pick 回 `feat/f-eco-b2-vfs-syscalls`（`dc3fdc1`）零冲突，两 leg 1062/0）

> busybox 试金石第八刀小件：`id`/`newgrp` 的补充组。login/su 复杂件留后续。
> **实现**：Task +groups[32]+ngroups（放 fpu_state **后**——其 offset 384 被 static_assert 钉死，插前会推后）。sys_getgroups(115)/setgroups(116)+do_ 变体放 sys_creds 家族；**do_ 取 Task* 参数**（测试内核 current()==null，F3-M4 GOTCHA#22）。getgroups size==0→count / size<count→EINVAL；setgroups root-only + count>32→EINVAL。
> **机制测 3**（栈 Task 直驱 do_）：getgroups count/array 精确匹配 + too-small→EINVAL + setgroups too-many→EINVAL + **非 root→EPERM**。
> **GOTCHA**：fpu_state offset 钉死→新 Task 成员须放其后；测试内核 current()==null→creds do_ 须取 Task*。follow-up：login/su（复杂件）/ CAP_SETGID 能力位 / 文件 group 权限查 supplementary groups。详见 [note](../notes/2026-07-01-f-eco-b8-groups.md)。push/PR 归用户。

## ✅ F-ECO 批7（socket syscall 全对齐）— 收官 2026-07-01（外包 worktree `feat/outsource-f-eco-b7-socket`，从集成线 `9683e38`，cherry-pick 回 `feat/f-eco-b2-vfs-syscalls`（`ec5ced6` 批7a + `2d2c9cc` 批7b）零冲突，两 leg 1059/0）

> busybox 试金石第七刀：socket(2) 族 14 个 syscall 全对齐（F7-M6 的 7 + 批7a 的 accept4/setsockopt/getsockopt + 批7b 的 getsockname/getpeername/shutdown/socketpair）。applet 端到端验收留 CI。
> **批7a（主控独占，handler-only）**：共享 helper 重构（socket_from_fd/install_socket_fd 移出匿名 ns + 提取 do_accept，sys_accept 退化、accept4 复用）；setsockopt 全选项 no-op accept；getsockopt SO_TYPE/SO_ERROR；accept4 SOCK_CLOEXEC→File::cloexec。
> **批7b（主控铺契约 + 3 分身并发，用户决策对治串行手撸）**：socket.hpp +SockAddrStorage +get_local/peer_addr 虚 +do_shutdown/shut_；3 分身零文件交叉填 Unix（+pair_with+socketpair）/ Udp / Tcp 子类 override + send/recv shutdown 检查；主控合并**一次编绿**。getsockname/getpeername 按 domain 拷 sockaddr；shutdown 记方向位子类入口检查。
> **机制测 8**：setsockopt/getsockopt/accept4-cloexec（批7a）+ getsockname path 精确匹配/getpeer_addr/shutdown 双向/socketpair round-trip（批7b）。
> **教训**：识别零文件交叉的并行面就并发分身（[[parallel-agents-rigorous-methodology]]），别手撸——批7b 三子类并发一次编绿。follow-up：socket-option 存储（setsockopt 真语义）/ SOCK_NONBLOCK / shutdown 对端传播 / DGRAM socketpair。详见 [note](../notes/2026-07-01-f-eco-b7-socket-alignment.md)。push/PR 归用户。

## ✅ F-ECO 批5（sysinfo + getrusage）— 收官 2026-07-01（外包 worktree `feat/outsource-f-eco-b5`，从集成线 `e2a7d70`，cherry-pick 回 `feat/f-eco-b2-vfs-syscalls`（`b4fa398`）零冲突，两 leg 1051/0）

> busybox 试金石第五刀内核件：`ps`/`free`/`uptime`/`top`/`time`。ps 走已有 ProcFS；free/uptime 用 sysinfo；time/top 用 getrusage。applet 端到端验收留 CI。
> **sysinfo(99)**：填 Linux struct sysinfo(112B)；uptime←HPET 秒、totalram/freeram←g_pmm×4096、procs←signal_nth_task_pid 计数、memunit=1；loads/buffer/swap/high=0（未跟踪诚实非编造）。**getrusage(98)**：填 struct rusage(144B) 全 0（无 per-task 会计）；who 校验 {-1,0,1}（RUSAGE_* 内核+libc 约定并集）。
> **机制测 4**：sysinfo sane（**cross-check totalram==g_pmm.total_page_count()\*4096** 证 PMM 接对）+ bad-ptr + getrusage zeros（poison 验清零）+ bad-who→EINVAL。
> **follow-up**：busybox applet 验收（CI）/ load average（调度器采样）/ swap 子系统 / per-task 资源会计（getrusage 真值）。详见 [note](../notes/2026-07-01-f-eco-b5-sysinfo-getrusage.md)。push/PR 归用户。

## ✅ F-ECO 批3 + 批4（nanosleep + dup/dup2/fcntl）— 收官 2026-07-01（外包 worktree `feat/outsource-f-eco-b3-b4`，从集成线 `9ef98e0`，cherry-pick 回 `feat/f-eco-b2-vfs-syscalls`（`3be9859`+`b271439`）零冲突，两 leg 1047/0）

> busybox 试金石第三、四刀的**内核件**：批3 `sleep`（nanosleep 35）+ 批4 `sh`+管道+重定向质变点（dup 32 / dup2 33 / fcntl 72）。busybox applet 端到端负载验收留 CI build。
> **批3 nanosleep**：`sys_nanosleep`/`do_nanosleep_kernel` 复用 clock_gettime 的 HPET monotonic（PIT 兜底）；`while(now<deadline) yield()`——**不 sti/hlt**（sti-in-syscall→#DF，yield 经 Task::ctx 安全）、**不 IRQ wake**（HPET 周期中断是 F5-M4 follow-up，现 poll+yield 正确不高效）；信号 -EINTR 不做（睡满，rem 清零）。do_ 内核变体供测试直调（is_user_vaddr 拒内核栈地址，sys_ 在测试内核过不了 copy_from_user）。
> **批4 dup/dup2/fcntl**：FDTable +dup/dup2（每次复制 = new File 独立描述，拷 inode+offset+flags+cloexec——hobby 简化避 File 引用计数；新 fd 达同 inode，pipe/重定向够用；Linux 共享描述符留 follow-up）；File +cloexec；sys_fcntl 子集 F_DUPFD/F_GETFD/F_SETFD/F_GETFL（F_SETFL/O_NONBLOCK 运行时改留 follow-up）。
> **机制测试防假绿（10 测）**：nanosleep duration(HPET delta≥5ms)+zero+bad-nsec；dup inode 共享+byte round-trip（InodeOps 内核缓冲直驱）+ dup2 redirect round-trip(fd42→pipe)+ dup2 same-fd no-op + fcntl cloexec round-trip + F_DUPFD + F_GETFL + bad-fd。
> **GOTCHA（测间 fd 表污染）**：第一轮批4 测全 PASS 但 test_vfs_close_invalid_fd 红——测试内核 current_fd_table() 全 suite 共享，批4 dup2 占了 fd42 没清 → 后续 close(42) 返 0≠-1。修法：每测结束 cleanup_fds() close 全部创建的 fd。启示：**ring0 测共享全局 fd 表，凡开 fd 的测须收尾 close**。详见 [note](../notes/2026-07-01-f-eco-b3-b4-nanosleep-dup-fcntl.md)。push/PR 归用户。

## ✅ F8-M3 AF_UNIX socket — 收官 2026-07-01（外包 worktree `feat/outsource-f8-unix-socket`，从集成线 `47573bc`，cherry-pick 回 `feat/f-eco-b2-vfs-syscalls` 零冲突，两 leg 1037/0）

> F8 IPC 第三里程碑（M1 Pipe + M2 FIFO 已在主线待 push）。AF_UNIX socket 收进 F7-M6 的 **Socket=InodeOps 适配器**——socket fd 仍是 `File→Inode→SocketOps`，`sys_read/write/close` 零改。
> **架构**：Socket base 加 path 虚函数 `bind_path`/`connect_path`（默认 NotImplemented；path 塞不进 AF_INET 形状的 `bind(port)`/`connect(addr,port)`，加法非破坏 Udp/TcpSocket 零改；listen/accept/send/recv 复用现有 virtuals）。`UnixSocket`（kernel/net/unix_socket.{hpp,cpp}）：bind_path 注册到内存 `UnixRegistry`（仿 FifoRegistry，**不依赖 tmpfs/ext2 真路径**，hobby-OS 简化）；connect_path 建 child、互连 peer、入 server accept 队列（立即建立无握手，确定性测试可 send-before-accept）；send 写 peer 的 4KB RX 环、recv 排干；阻塞走 `prepare_to_wait/schedule_blocked`（不 sti/hlt，避 sti-in-syscall #DF）。锁序：registry 锁绝不与 socket 锁嵌套（无 AB-BA）；peer_ 写一次、send 快照后只持 peer 锁。sys_socket 收 AF_UNIX 直接 `new UnixSocket`（自洽无栈依赖，避开改 net_init.cpp+net_stub.cpp 两处）。
> **机制测试防假绿（4 测）**：returns_fd（sys_socket 创 UnixSocket）+ loopback echo（connect→send→accept→recv 逐字节精确匹配双向）+ connect 未绑→NotFound + bind 重复→AlreadyExists。echo + 负测走 UnixSocket 直接方法（同 AF_INET TCP/UDP echo——`copy_from_user` 的 `is_user_vaddr` 范围检查拒内核地址，测试内核栈 sockaddr_un 走 sys_bind 会 -EFAULT；sys_bind 的 sockaddr_un 解析留生产 musl 覆盖）。
> **follow-up**：fs-backed bind() 接 DevFS/tmpfs 真节点 / abstract socket / socketpair + AF_UNIX DGRAM sendto(path) / send-side flow control（ring 满现返 EAGAIN）/ SMP peer 可见性 rigor / connect 阻塞到 accept / close 无 InodeOps::release（listener 不撤名）。详见 [note](../notes/2026-07-01-f8-m3-unix-socket.md)。push/PR 归用户。

## ✅ F-ECO 批2（VFS metadata + dirent 8 syscall）— 收官 2026-07-01（分支 `feat/f-eco-b2-vfs-syscalls`，从 main `470e9c8`，两 leg 1033/0）

> busybox 试金石第二刀（[todo f-eco/00-busybox-touchstone](../todo/f-eco/00-busybox-touchstone.md) 批2）：为 `cp/mv/rm/touch/ln/mkdir/chmod/chown` 普及 8 个 Linux syscall —— `rename`(82)/`symlink`(88)/`link`(86)/`readlink`(89)/`chmod`(90)/`chown`(92)/`umask`(95)/`utimensat`(312)。
> **严密分身并行（用户决策，对治"prompt 不全→分身改乱"）**：阶段0 主控独占铺公共契约（`InodeOps` 7 default virtual + Ext2 方法/override 声明 + ext2 NotImplemented 桩 + 8 完整 sys handler 走 lookup→派发 + syscall 号/注册/CMake）；阶段1 两分身填 ext2 方法体（块A 独占 `ext2_common.cpp` setattr+readlink / 块B 独占 `ext2_directory.cpp` symlink+link+rename，零文件交叉）。先读全签名模式再铺契约；分身 prompt 注入完整契约 + Ext2 原语清单 + 严格单文件边界 + 禁碰公共头 + 不跑 cmake（主控统一编译）。
> **机制测试防假绿（价值实证）**：`run_syscall_ext2_tests` 加 6 测（chmod/chown/utimensat/symlink+readlink/link/rename 验语义）抓出两类真 bug——① stat 读 inode cache 不读盘，setattr 写盘须 `invalidate_cached_inode`；② `populate_vfs_inode` 不认 `S_IFLNK` → 符号链接 ops=nullptr → readlink -EIO。分身编绿 + 基线零回归，但 stat 假失败——"绿≠对"。修后两 leg 1033/0（1027+6）。
> **follow-up**：busybox -O2 真实测（本地无 build，留 CI）/ rename overwrite + 目录 nlink / umask 应用到 create / symlink follow / chown 16 位 / utimensat nsec+wall-clock。详见 [note](../notes/2026-07-01-f-eco-b2-vfs-syscalls.md)。push/PR 归用户。

## ✅ F7-M6 Socket API 收官（BSD socket 系统调用 / Socket=InodeOps 适配器）— 2026-06-30（worktree `worktree-f7-m6-socket`，从干净 main `f1f29aa`，两 leg 1027/0）

> Feature 域 F7 最后一里程碑。接 F7-M5 TCP ✅（已合 main PR#53）+ F7-M4 UDP ✅（PR#47）。网络线走到「可用」：用户态程序能用 socket()/connect()/send()/recv() 收发数据。
> **架构（4-agent 设计面板核实，已读码）**：socket fd 走 **InodeOps 子类**（`SocketOps`），对齐 PTY/pipe——socket fd → File → Inode → SocketOps，`sys_read/write/ioctl/close` **零改动**（对 fd 层 socket fd 就是一个 pipe fd）。`socket()` 装合成 Inode（抄 `sys_pipe::do_pipe_kernel` unique_ptr→release），`accept()` 抄 `PtmxOps::open` cloning 再发一个 fd。TcpModule/UdpModule **保持纯协议层不动**；per-socket RX 环 + 阻塞 + accept 队列放 Socket 适配器，挂 listener 缝（`UdpListener::on_udp` / `TcpListener::on_accept/on_data/on_close`）——回调里**拷贝借来的帧**进环（设备 dispatch 后回收 buffer），send 直接调 module。阻塞用现成 `prepare_to_wait/schedule_blocked/unblock`（F8 pipe 那套），从 `net_poll` kthread 上下文唤醒，**不需 timer**。生产 `net_init.cpp` `add_l4(kIpProtoUdp/...)`+`add_l4(kIpProtoTcp/...)` 注册 + 挂 LoopbackDevice + dev 路由 accessor。
> **范围栅栏（不投机）**：① TCP 重传/RTO/窗口/拥塞 defer（TcpModule 一个没有，需内核 timer=HPET 周期中断=F5-M4 follow-up，硬前置非 M6 活）② TIME_WAIT/ISN 随机化/乱序重组 ③ IPv6/AF_UNIX（F8）/sendmsg cmsg ④ epoll/select（F8）/第二 NIC/中断驱动 RX（仍 net_poll 轮询）。→ **最小可用 = loopback 端到端干通 + SLIRP 尽力**（loopback 零丢包；SLIRP TCP 无重传可能丢）。
> 验证：每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg 绿（本地 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF -DCINUX_BUILD_TESTS=ON` 关 smoke 防挂死 + 编 host 测）；改公共头（`InodeOps` 不动虚表只加子类）push 前补全量 `cmake --build build`。

### 批表
| 批 | 范围 | 状态 | 测试 |
|----|------|------|------|
| 0 | 立项 docs（本段）+ ROADMAP F7-M6 ⏳→🔄 + todo `05-socket.md` 重写（范围栅栏 + 架构图 + B0-B4） | ✅ | docs-only |
| 1 | 底座：生产注册（`g_udp`/`g_tcp` + `add_l4` + 挂 loopback + dev 路由 accessor）+ Socket base（端点/proto/per-socket RX 环/等待队列/virtuals，纯逻辑 host 可测）+ SocketOps InodeOps 缝 + `sys_socket/bind/connect/listen/accept/sendto/recvfrom/close` 通用派发（via Socket virtuals）+ syscall 号注册（41-55/288，syscall_nums.hpp 已核实空）。内部可拆 B1a(net_init+base)/B1b(fd+syscall) | ✅ B1a `b9d2057` / B1b `66345c2` | 两 leg 1025/0（+5 socket 桩）+ 生产 boot 冒烟 + 解耦绿 |
| 2 | UDP socket：UdpSocket 适配器（`on_udp`→拷贝进环、bind/sendto/recvfrom、阻塞、未绑自动 ephemeral）+ loopback echo | ✅ `c7d2eae` | 两 leg 1026/0（+1 UDP loopback 双向 echo）+ 解耦绿 |
| 3 | TCP socket：TcpSocket 适配器（accept 队列、`on_data`→per-连接环、listen/accept/connect/send/recv、阻塞、accept 建 child + `TcpModule::set_listener` 重绑 TCB listener）+ loopback echo | ✅ `9ca84f4` | 两 leg 1027/0（+1 TCP loopback echo:connect→握手→accept→双向）+ 解耦绿 |
| 4 | 收官（note + ROADMAP F7-M6 ✅ + 本段）。**loopback TCP/UDP echo 回归门已在 B2/B3 落地**；musl 静态 socket 程序作 follow-up（需 worktree sysroot + net_poll-kthread-aware 测试） | ✅ 本次 | docs-only；内核回归门见 B2/B3 |

### 风险 / 陷阱
- **borrowed frame 必须拷贝**：`on_udp`/`on_data` 的 FrameView 借自设备 buffer，dispatch 返回后回收；回调里必须 copy 进 per-socket 环，否则 recv 拿到垃圾（udp.hpp/tcp.hpp 注释明示）。
- **生产 add_l4 容量**：`Ipv4Module::kMaxL4=4`（ICMP ctor 自动注册占 1），UDP+TCP=3 槽放得下。
- **阻塞 vs sti/hlt（致命 GOTCHA）**：socket recv/accept 在 syscall 上下文**绝不 sti/hlt**（→ #DF，见 memory sys-ping-df-sti-in-syscall）；走 `prepare_to_wait/schedule_blocked`，由 net_poll kthread 唤醒（同 F8 pipe）。
- **解耦门** `check_net_decoupling.sh`：`kernel/net/` 不能 include driver/dma/irq。Socket 抽象放 kernel/net/（只用 net 模块 + scheduler 等待队列）；实例化 + add_l4 + accessor 放 net_init.cpp（drivers/net/）。
- **loopback 必挂**：现在生产只挂 e1000/SLIRP，127.0.0.1 不通；B1 挂 LoopbackDevice（确定性测试关键，无 QEMU/SLIRP 时序依赖）。
- **accept 路由**：TCP 服务端一个 listen 端口多对端；Socket 层须建 remote 4-tuple→per-连接 Socket 映射，`on_data`/`on_close` 据此路由到对的 fd。
- **smoke 默认 ON + 本地无 build/musl → 挂死**：本地 `CINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF`（同 F10 惯例）。
- **host ASAN**：Socket 无 release 钩子则 host 测试自释放 per-open 资源（参 F8 test_fifo 教训，CI host-tests CINUX_HOST_ASAN 抓泄漏）。

## ✅ F8-M1 Pipe 增强 + F8-M2 命名 FIFO — 收官 2026-06-30（worktree `worktree-f8-pipe-fifo`，从干净 main `c0188cd`，7 commit 待 push）

> Feature 域 F8 IPC 扩展前两里程碑。接 F10-M3 PTY(InodeOps::open cloning 扩展点已就位)+ F6-M3 DevFS(add_node/set_dynamic_lookup 已就位)。**M1=匿名 pipe 修齐**:① reader-gone 改返 `Error::BrokenPipe`(现 pipe_ops.cpp:43 返 IOError→kEio,故 sys_write.cpp:53-59 的 EPIPE→SIGPIPE 从未真触发)② 真调度阻塞替 sti/hlt 自旋(致命 GOTCHA:sti-in-syscall→#DF,走 prepare_to_wait/schedule_blocked,对齐 console_tty/Mutex)③ O_NONBLOCK→EAGAIN。**M2=命名 FIFO**:FifoRegistry + FifoOps(open() cloning 首读者建 Pipe)+ sys_mknod/mkfifo。依赖全在 main(SYS_pipe=22、signal_send/killpg、InodeOps::open、DevFs add_node);缺 SYS_mknod(M2 新建)。范围栅栏:不做 Unix Socket(M3)/shm(M4)/epoll(M5);用户态 shell 真闭环留后续;ConditionVariable 抽象留 sync 里程碑(本里程碑复用现成 wait queue)。
> 验证:每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg 绿(本地 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF` 关 smoke 防挂死);改公共头(InodeOps/File)push 前补全量 `cmake --build build`。

### 批表
| 批 | 范围 | 状态 | 测试 |
|----|------|------|------|
| 0 | 立项 docs(本段)+ ROADMAP F8-M1/M2 ⏳→🔄 + todo `00-pipe.md`/`01-fifo.md` 重写(选型:直接 wait queue 替 sti/hlt,非 ConditionVariable) | ✅ | docs-only |
| 1 | BrokenPipe 修(`PipeWriteOps::write` reader-gone→`Error::BrokenPipe`,零新接口,`reader_alive()` 区分)+ 负测验 SIGPIPE 真触发(CurrentTaskGuard + do_write_kernel → -kEpipe + sig_pending 含 kSigpipe) | ✅ | run-kernel-test-all 两 leg 987/0 + host sys_pipe 断言 BrokenPipe |
| 2 | 真调度阻塞替 spin(Pipe 加 read/write 等待队列 + prepare_to_wait/schedule_blocked/unblock;host `#ifndef CINUX_HOST_TEST` 不阻塞)+ O_NONBLOCK(Pipe::read/write `nonblock` 参 + kWouldBlock→WouldBlock→kEagain) | ✅ | run-kernel-test-all 两 leg 990/0(+3 nonblock 负测) + host pipe/sys_pipe 零警告 |
| 3 | FIFO 核心 `fifo.{hpp,cpp}`(FifoRegistry + FifoOps open() cloning 首读者建 Pipe + fs_private 存 Fifo*)+ host 单测 | ✅ | host test_fifo 4/0 + 两 leg 990/0(open() 签名 +flags 零回归,PTY 全绿) |
| 4 | mknod/mkfifo syscall(`SYS_mknod=133` + S_IFIFO→FifoRegistry + 注册) | ✅ | run-kernel-test-all 两 leg 990/0(DevFS dynamic_lookup 加 FIFO 先查零回归) |
| 5 | 跨 fd round-trip + mkfifo 真测 + 两 leg + note + ROADMAP F8-M1/M2 ✅ | ✅ | run-kernel-test-all 两 leg 993/0(+3 FIFO: round-trip + ENOSYS + EEXIST) |
| 6 | **shell 端验证补批**(用户要 shell 上验):user/libc `sys_mknod` wrapper + S_IFIFO/O_* + `cmd_mkfifo` + `cmd_fifotest`(单进程 round-trip smoke)+ 注册/help/CMake。**零内核改动**(user open 走 SYS_open→do_open_kernel 已 cloning,do_openat cloning 留 F10-M4 follow-up) | ✅ | run-kernel-test-all 两 leg 993/0 零回归 + user_shell 编译绿(fifotest/mkfifo 链入);GUI 交互验证归用户 |

### 风险 / 陷阱
- **批2 头号风险(致命 GOTCHA)**:sti-in-syscall→#DF(见 memory sys-ping-df-sti-in-syscall)。现状 `Pipe::write/read` 在 syscall 上下文 `irq_enable();hlt()` 自旋 → 真硬件 #DF(harness 不真跑 ring3 阻塞故抓不到)。正解别 sti/hlt,走 prepare_to_wait/schedule_blocked。
- **批2 host 兼容**:pipe.cpp 链进 host 单测(无 scheduler);Scheduler 调用包 `#ifndef CINUX_HOST_TEST`(对齐 irq.hpp 现成 host-guard,§14 rule 3 合法),host 路径不阻塞(同步单测本就不触发满/空)。
- **O_NONBLOCK 透传**:InodeOps::read/write 签名不带 flags(改签名 blast radius 大,PTY 刚动过);匿名 pipe(2)不带 flags 故 sys_pipe 端始终 blocking;O_NONBLOCK 经 FIFO open()(M2)落地,pipe 层先备 nonblock 参。
- **InodeOps::open cloning** 是 PTY 加的扩展点(默认返原 inode),FIFO 复用;**fs_private 存 Fifo\*** 避免改 InodeType enum(诚实的 hobby-OS 简化)。
- **smoke 默认 ON + 本地无 build/musl → 挂死**:本地一律先 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF`。

## ✅ F7-M5 TCP（传输层：三次握手 / 序号-ACK / 四次挥手 + 伪首部校验和）— 收官 2026-06-30（分支 worktree-f7-m5-tcp 待 PR）

> worktree `worktree-f7-m5-tcp`（从干净 main `c0188cd`，接 F7-M4 UDP ✅）。在 IPv4 同层加 TCP：`TcpModule : L4Handler`（proto=6）经 `ipv4.add_l4(kIpProtoTcp, tcp)` 入 L4 proto 表（M4 已立单一分派机制，加协议不疼）。**范围（用户拍板）**：TCP 状态机（三次握手 / 序号-ACK / 四次挥手）+ 伪首部校验和；**最小可用（无内核 timer-wake）——单方向数据 + 无重传**；真重传/RTO/滑动窗口/拥塞控制、TCP Socket（listen/accept/recv）留 follow-up（Socket 进 F7-M6）。**栅栏**：不碰 socket/syscall（M6）、不进 production `net_init.cpp`（无消费者，同 M4 UDP）、test-only（host 单测 + loopback 内核 round-trip + e1000 TX smoke）。
> 验证：每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg 绿（基线 **986/0**——main c0188cd 已含 F10-M3 PTY；**本地必须** `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF -DCINUX_BUILD_TESTS=ON` 防 smoke 挂死 + 编 host 测）；改公共头（ipv4.hpp 加 `kIpProtoTcp`）push 前补全量 `cmake --build build`。**并发会话占 VNC :0**：本机多 worktree 会话全用硬编码 `-vnc :0`，本 worktree 验证临时切 `-vnc :5`、跑完 `git checkout -- cmake/qemu.cmake` 还原（验证 hack，非代码改动）。

### 设计要点（决定打法，已读码核实）
- **poll() budget loop（64）单次排干 loopback 上整个握手+数据+挥手**：每步 reply 在 dispatch 期间经 `send_l3` 入队、下一 budget 轮排干（[net_stack.cpp:108](../../kernel/net/net_stack.cpp#L108)）。TCP 多包交换因此能在确定性 loopback 上端到端验证，无需 SLIRP/timer。
- **伪首部校验和**：TCP 与 UDP 同用 12 字节伪首部（src/dst IP + 0 + proto + L4 长度），proto=6。沿用 UDP 连续缓冲区法 [伪首部|TCP 头|payload] 一把 `internet_checksum`（[udp.cpp](../../kernel/net/udp.cpp) 范式）。TCP 校验和**必填**（与 UDP 不同，checksum=0 不是「无校验和」）。
- **ISN**：简单递增计数器（确定性，测试可复现）；ISN 随机化（安全，对齐 F9 ASLR 同族）留 follow-up。
- **连接表**：固定表（kMaxTcpCons），4-tuple 键（local/remote addr+port）。loopback 上客户端/服务端两条记录同居一表——一表跑双端状态机，测试干净。

### 批表
| 批 | 范围 | 状态 | 测试 |
|----|------|------|------|
| 0 | 立项 docs（本段）+ ROADMAP F7-M5 🔄 + todo `04-tcp.md` 范围栅栏 | ✅ `405f445` | docs-only |
| 1 | tcp.hpp wire（TcpHeader 20B + SYN/ACK/FIN/RST/PSH/URG + parse/build + data-off）+ `ipv4.hpp` 加 `kIpProtoTcp=6`（不改 add_l8）+ tcp.cpp 骨架（伪首部校验和门 + handle 诊断）+ CMake + host 单测（头 round-trip/flags/校验和 round-trip/坏校验和 drop） | ✅ `30a136a` | host net_tcp 4/0 + run-kernel-test-all 两 leg 各 986/0（零回归） |
| 2 | 连接表 + `listen`/`connect` + 握手 FSM（SYN→SYN-ACK→ACK，序号-ACK 算术）+ RST（SYN 到未监听端口）+ host mock 验时序 | ✅ `fb9f4b8` | host net_tcp 7/0（+3）+ run-kernel-test-all 两 leg 各 986/0 |
| 3 | `send`（数据段 + ACK 推进）+ `close`（FIN）+ 数据/挥手 FSM（4-way）+ host 单测（established 数据 round-trip + 挥手） | ✅ `ec0aa78` | host net_tcp 9/0（+2）+ run-kernel-test-all 两 leg 各 986/0 |
| 4 | loopback 内核 round-trip（test_net.cpp `test_tcp_loopback`：单 poll 排干握手+数据+挥手）+ e1000 TX smoke（`test_tcp_e1000_tx`：发 TCP 段，ARP resolve+send ok） | ✅ `cf0db3e` | run-kernel-test-all 两 leg 各 988/0（+2 TCP）+ check_net_decoupling 绿 |
| 5 | note + ROADMAP F7-M5 ✅ + PLAN 收官 | ✅ | docs-only |
| 6 | 对抗性加固：handle 头长度 sanity 门（data_off<5 / >payload 丢）+ host 6 负测（截断头/畸形 data_off×2/连接表满/迷路 ACK/乱序数据） | ✅ `24bd4e4` | host net_tcp 15/0（+6）+ run-kernel-test-all 两 leg 各 988/0 |

> **F7-M5 收官**（2026-06-30,worktree-f7-m5-tcp 12 commit 待 PR）：TCP 协议层——`TcpModule : L4Handler`（proto=6）经 `ipv4.add_l8` 入 L4 表。6 批:wire+校验和门(批1)→握手 FSM+RST(批2)→数据+四次挥手(批3)→内核 loopback 端到端+e1000 TX smoke(批4)→收官(批5)→对抗性加固+负测(批6)。**架构**:沿用 UDP 范式(L4Handler 缝 + 连续缓冲区伪首部校验和),加协议不疼。**最小可用诚实标注**:无重传/RTO/滑动窗口/拥塞控制(需 timer 基建)、ISN 随机化(安全,follow-up)、TCP Socket(listen/accept/recv 进 F7-M6)。范围栅栏:不进 production net_init(无消费者)、test-only。验证:host net_tcp 15/0 + run-kernel-test-all 两 leg 各 988/0 + check_net_decoupling 绿。**当前不可达**(生产不注册 TCP)故零崩风险;批6 给 M6 接进生产后兜底(坏段不崩)。详见各批 notes。**push/PR 归用户**。

### 风险 / 陷阱
- **状态机复杂度**：TCP 是网络层最复杂协议。缓解：按批递进（wire→握手→数据/挥手），每批独立绿；最小可用（无重传/窗口/拥塞）诚实标注，不包装成完整 TCP。
- **校验和门**：handle 收到的 payload 就是整个 TCP 段（IP 已剥头）；伪首部用 TCP 段长（头+数据）。TCP 校验和必填，0 不跳过。
- **loopback 单 poll 多包**：握手 3 包 + 数据 + 挥手靠 budget loop 排干；某步不生成下一段会早停——测试观察 state 推进断言。
- **栅栏**：不进 production `net_init.cpp`（同 UDP，无消费者）；socket/accept/recv 留 M6；smoke 默认 ON 挂死，本地 OFF。

## ✅ F6-M2 ProcFS（/proc 进程自省虚拟文件系统）— 收官 2026-06-30（分支 worktree-f6-m2-procfs 待 PR）

> Feature 域 F6 VFS 第二里程碑。接 F6-M3 DevFS（已合 main PR#48）—— 照抄 DevFs 范式（`FileSystem` 子类 + 匿名 namespace `InodeOps` 子类 + boot 接线单独 `procfs_init.cpp`），把内核 Task registry 暴露成 `/proc`。**范围栅栏（不投机）**：本里程碑只做**进程自省**——`/proc` 根 readdir 枚举 pid + `/proc/<pid>/` 目录 + `/proc/<pid>/{stat,cmdline}` 动态伪文件。**不做**静态信息节点（`/proc/version`/`meminfo`/`cpuinfo`/`uptime`）、不做 `/proc/<pid>/{maps,fd,status}` —— 留 follow-up（见 todo `01-procfs.md`）。分支 `worktree-f6-m2-procfs`（worktree 从干净 main `c0188cd`）。
> **设计（照抄 DevFs + 适配动态语义）**：`ProcFs`（`FileSystem` 子类，内存型虚拟 FS）。DevFs 根目录是定长节点表；ProcFs 根目录条目是**动态 pid**（Task registry 实时快照），故 readdir 走新增 `signal_enumerate_task_pids` accessor（`g_registry_lock` 下快照，**纯增量**不改 register/unregister/find_by_pid）。叶 inode 身份：`PID_MAX=256` 有界，ProcFs 持 `pid_dir_inodes_[257]`/`stat_inodes_[257]`/`cmdline_inodes_[257]` 定长池（pid 索引），`ino=pid`、`fs_private=this` —— **SMP 安全**（每 pid 稳定 inode，并发 lookup 不竞态）+ **无泄漏**（close 只删 File 不删 Inode，inode 归 ProcFs，对齐 DevFs）。lookup strip `/` 后解析 `<pid>`/`<pid>/stat`/`<pid>/cmdline`，pid 经现有 `signal_find_task_by_pid` 校验存活（非活 → NotFound，对齐 Linux 只露活进程）。
> **测试边界（诚实）**：procfs.cpp 读 kernel Task registry（非纯逻辑，依赖 `signal.hpp`/`process.hpp`），**不能 host 单测**（DevFs 靠注入 CharSink 才 host-testable；ProcFs 直读 registry 无注入缝）—— 走 kernel harness（QEMU `run_procfs_tests`）。测试自建栈 Task 注册已知 pid（不依赖 registry 残留状态），确定性验证 readdir 枚举 + stat 内容。
> 验证：每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg 绿（本地先 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF` 关 smoke 防挂死）；批 1+ 改公共头（signal.hpp）push 前补全量 `cmake --build build`。**⚠️ 根目录无 Makefile——一律 `cmake --build build --target <name>`**。
### 批表
| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 0 | 立项 docs（本段）+ ROADMAP F6-M2 ⏳→🔄 + todo `01-procfs.md` 标范围栅栏 | ✅ | `5fd7e52` | docs-only |
| 1 | procfs 骨架：`procfs.hpp`/`procfs.cpp`（`ProcFs`+mount/lookup root+`<pid>` 目录，用现有 `signal_find_task_by_pid` 校验存活；`ProcRootDirOps` readdir 暂硬编码 `"."`/`".."`）+ `kernel/fs/CMakeLists.txt` + `kernel/test/test_procfs.cpp`（mount/lookup/stat root+pid-dir）+ `main_test.cpp` 注册 | ✅ | 本次 | run-kernel-test-all 两 leg 991/0（+5 procfs） |
| 2 | `signal_nth_task_pid` accessor（signal.hpp/cpp，纯增量；锁内走到第 n 个 task）+ `ProcRootDirOps::readdir` 真枚举 pid + 测（readdir 列出注册 pid；注销后消失）。**frame-safe**：原 `signal_enumerate_task_pids` 快照版在 readdir 栈上 `int pids[257]`=1028B 超 kernel `-Wframe-larger-than=1024`，改 nth 版（锁内走到 index，无栈数组） | ✅ | 本次 | 全量编绿 + run-kernel-test-all 两 leg 992/0（+1 enumerate） |
| 3 | `/proc/<pid>/` 子目录：`ProcPidDirOps`（readdir `"stat"`/`"cmdline"`）+ `ProcStatFileOps`/`ProcCmdlineFileOps`（动态伪文件，read 时 `signal_find_task_by_pid`→Task 字段生成内容；`/proc/<pid>/stat` = `pid (name) state ppid tgid uid gid`，`/proc/<pid>/cmdline` = name 尽力）+ lookup `<pid>/stat`/`<pid>/cmdline` + 测（read stat 内容含 pid+name；dead pid read→NotFound）。**拆文件**：procfs.cpp 504 行超 500 软限，按职责拆出 `procfs_content.{hpp,cpp}`（Task→文本生成器），procfs.cpp 留 FS plumbing（406 行） | ✅ | 本次 | 全量编绿 + run-kernel-test-all 两 leg 997/0（+5 伪文件测） |
| 4 | boot 接线：`procfs_init.cpp`（`procfs::init()` mount+`vfs_mount_add("/proc")`）+ `kernel/fs/CMakeLists.txt`(+procfs_init) + `init.cpp` 挂 procfs::init()（devfs::init 后）+ `make run` 冒烟（/proc 装配零 panic） | ✅ | 本次 | run-kernel-test-all 两 leg 997/0 回归 + 生产 boot 冒烟 `[PROCFS] mounted at /proc` 零 panic |
| 5 | 收官：note + ROADMAP F6-M2 ✅ + PLAN ✅ + todo `01-procfs.md` 打勾（本批范围） | ✅ | 本次 | docs-only |

### 风险 / 陷阱
- **Inode 身份/生命周期**：close 只删 File 不删 Inode（file.cpp 验证），故 ProcFs 必须自持叶 inode 生命。定长 pid 索引池解决（每 pid 稳定 inode）。若用「单 scratch inode 复用」→ 并发同 kind lookup 竞态覆写 pid → 错读（明确**不**这么做）。
- **registry TOCTOU（已知，留 follow-up）**：`signal_find_task_by_pid` 返指针后 unlock，read Task 字段时 task 可能已 exit+free（DEBT-002 RCU-safe registry 未修）。窗口极小（find 后立即快照字段），hobby OS 可接受，note 标注；真修随 DEBT-002。
- **测试内核主线程不经 add_task**（main_test.cpp 在 boot 栈直跑，不进 registry）→ registry 可能空 → readdir 测**自建栈 Task 注册已知 pid**保证确定性（不依赖 registry 残留；参 F3-M4 GOTCHA#22 栈 Task 思路）。
- **`/proc/<pid>/cmdline` 范围诚实**：CinuxOS Task 不存 argv，cmdline 返 `name`（尽力，非真 argv）；note 标注。
- **stat 内容是简化版**（非完整 Linux /proc/<pid>/stat 52 字段，CinuxOS 无 accounting）—— 格式 `pid (name) state ppid tgid uid gid`，note 标注简化。
- **smoke 默认 ON + 本地无 `build/musl/hello` → 挂死**：本地 `CINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF`（同 F10-M2/M3 惯例）。
- **架构契合**：A.6 无异常（伪文件生成走 ErrorOr，read 返 ErrorOr<int64_t>）；A 子模块边界（用 Cinux-Base ErrorOr，不自建）；对齐 Linux（procfs 动态 pid 目录 + /proc/<pid>/stat 语义，简化字段诚实标注）；§14（procfs_init.cpp 单独文件，CMake 决定编不编，源码零 #ifdef）。

## ✅ F10-M2 follow-up（ext2 double-indirect 块映射）— 收官 2026-06-30（分支 worktree-ext2-indirect 待 PR）

> F10-M2 收官登记的 follow-up（见下「F10-M2」段 follow-up 列表）：ext2 块映射只处理 direct + single-indirect，>268KB(1024 块)/>4MB(4096 块)文件读写会截断；F10-M2 批3 把 QEMU ext2 image 改 4096-byte 块规避（interp 822KB 落进 single-indirect 4MB 上限）。本弧**真修**：`get_or_alloc_block`(写) + `read`(读)补 double-indirect 分支(i_block[13])，撤 `create_ext2_disk.sh` 的 4096 workaround。分支 `worktree-ext2-indirect`（从干净 main `c0188cd`，零文件冲突弧）。
> 范围栅栏：改动严格圈在 [ext2_common.cpp](../../kernel/fs/ext2_common.cpp)::read + [ext2_inode.cpp](../../kernel/fs/ext2_inode.cpp)::get_or_alloc_block 两函数内；不碰 InodeOps/PageCache；triple-indirect(slot 14) 不做（>16GB 文件，hobby OS 用不到）。
> 验证：每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg 绿（基线 ~986/0）；本地 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF`；改公共块映射（get_or_alloc_block 签名不变，仅函数体）push 前 host 单测必跑（CI 盲区：run-kernel-test 不编 test/unit/）。

### 批表
| 批 | 范围 | 状态 | 测试 |
|----|------|------|------|
| 0 | 立项 docs（本段）+ F10-M2 follow-up bullet 标 in-progress | ✅ `7f27c32` | docs-only |
| 1 | 写路径：`get_or_alloc_block` 加 i_block[13] double-indirect 分支（offset=file_block-DIRECT-PTRS、idx1=offset/PTRS、idx2=offset%PTRS，三层 lazy alloc，scratch buf 逐层落盘） | ✅ | run-kernel-test-all 两 leg 762/0 零回归（新分支未被现有用例触发） |
| 2 | 读路径：`read` 加 double else-if（纯读三层，hole 零填）+ 放开 write 的 file_block 门（`>EXT2_DIRECT_BLOCKS` 不再 break，扩到 double-indirect 上限） | ✅ | run-kernel-test-all 两 leg 762/0 零回归 |
| 3 | host 单测：`test_ext2_ops.cpp` 的 `host_file_write`/`host_file_read` 补 single+double indirect（镜像 kernel 算法）+ >single-indirect-ceiling round-trip 用例 | ✅ | test_ext2_ops 30/0（+1 file_indirect）+ 全 host ctest 65/0 |
| 4 | 撤 workaround：`create_ext2_disk.sh` BLOCK_SIZE 4096→1024 + 注释更新（double-indirect 已支持）+ 两 leg + boot 冒烟 + note | ✅ | run-kernel-test-all 两 leg 762/0 + make run `block_size=1024` ext2 mounted + shell 提示符零 panic + note |
| 5 | docs：建 musl sysroot（`build-musl.sh` + `build-hello-dyn.sh`，gcc 16.1.1 编通）开 `CINUX_MUSL_DYN_SMOKE` —— 822KB ldso 装盘读它真走 i_block[13]，double-indirect 真内核覆盖 | ✅ | run-kernel-test-all 两 leg dyn smoke 各 `hello-dyn 5/5 iters PASS` + 串口 5× Hello（无 `segment read failed at offset 274432`） |

> **收官（2026-06-30）**：ext2 块映射补 double-indirect(i_block[13]) 读+写，撤 4096 workaround（盘改回 1024-byte 块）。6 commit（批0 docs `7f27c32` / 批1 写路径 `8683efd` / 批2 读路径+门 `fd6008f` / 批3 host 单测 `cff0cb1` / 批4 撤 workaround / 批5 docs dyn smoke 真内核覆盖）。验证：run-kernel-test-all 两 leg 762/0 + host ctest 65/65（+1 double-indirect round-trip）+ make run 真 boot `block_size=1024` 零 panic + **musl dyn smoke 两 leg 各 5/5 PASS（822KB ldso 真走 i_block[13]，无 segment read fail）**。triple(slot 14) 显式不做（>16 GB 文件）。详见 [note](../notes/2026-06-30-f10-m2-followup-ext2-double-indirect.md)。**push/PR 归用户**。

### 风险 / 陷阱
- **block_buf_ 单 scratch（头号 GOTCHA）**：多层遍历 read_block 反复覆盖同一 4KB buf。double 写须严格顺序——alloc+写盘下层后，**读回上层再填指针**（write_block 已把 buf 改成下层内容），逐层落盘再下钻。仿现有 single-indirect read-modify-write（[ext2_inode.cpp:354-361](../../kernel/fs/ext2_inode.cpp) 二次 `read_block(indirect_blk)` 即此模式）。错则毁 inode 元数据。
- **CI 盲区**：run-kernel-test 不编 test/unit/ host 单测；改公共块映射（即便签名不变）push 前补 `cmake --build build -j$(nproc)` 全量 + `test_host`。
- **double-indirect 双覆盖**：host 单测（批3，算法 round-trip）+ **musl dyn smoke（批5，真内核）**——建 sysroot 开 `CINUX_MUSL_DYN_SMOKE`，822KB ldso 装盘后 execve 读它真走 i_block[13]，两 leg 各 5/5 PASS（无 `segment read failed at offset 274432`）。shell 17KB/motd/hello.txt 都 direct，indirect 路径靠这两层守。
- **4096→1024 撤 workaround 风险低**：所有现存盘上文件（shell 17KB / motd / hello.txt）远低于 268KB single-indirect 上限，纯 direct 块，零行为变；mount() 早已支持 1024 块。
## ✅ F5-M4（HPET + RTC 定时器）— 收官 2026-06-30（分支 worktree-f5-m4-hpet-rtc 待 PR）

> Feature 域 F5 第四里程碑。worktree `worktree-f5-m4-hpet-rtc`（从干净 main `c0188cd`）。目标：给内核一个**高精度 monotonic 时间源（HPET）+ 真墙钟（RTC）**，替掉 `sys_clock_gettime` 现在两边都读 PIT uptime、无墙钟的现状（`sys_clock_gettime.cpp:37`）。PIT **不动**——它仍独占 BSP 抢占（`PIT::irq0_handler→Scheduler::tick`）；本里程碑只加新时间源，不抢 PIT 的活。
> **打法（用户拍板，预批准自主推进）**：ACPI HPET 表解析（`HPETHeader`+`parse_hpet` 照抄 `parse_madt`）→ HPET MMIO 驱动（`@KMEM_MMIO+0x60000`，`FLAG_PCD`，读 free-running counter → monotonic ns）→ RTC 端口 I/O（`0x70/0x71`，BCD→binary，UIP，转 Unix epoch）→ 接 `sys_clock_gettime`（MONOTONIC←HPET，REALTIME←RTC boot base + HPET delta，即「漂移修正」）。
> **范围栅栏（不投机）**：先做 **free-running 计数器纯读**，不接 HPET 周期中断（留 follow-up，需配 timer comparator + IRQ vector，类比 LAPIC timer）；不做 nanosleep / gettimeofday（todo T4 留后续）；不建 `kernel/lib/time.hpp` 大杂烩（YAGNI，两驱动自持状态，sys_clock_gettime 组合）；RTC 周期重同步（NTP 式）需 IRQ，留 follow-up。
> **依赖（都在 main）**：`acpi::find_table`（`acpi.hpp:92`，注释已列 HPET）是 HPET 表发现缝；`KMEM_MMIO` 窗 + `FLAG_PCD`（`memory_layout.hpp:47` / `paging_config.hpp:36`，xHCI/e1000/LAPIC 已实证 uncached MMIO 范式）；QEMU 'pc' 默认带 HPET 表 + 设备 @ `0xFED00000`（`cmake/qemu.cmake` 无 `-no-hpet`）。
> 验证：每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg 绿（本地 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF` 关 smoke 防挂死）；改公共头（`acpi.hpp`）push 前补全量 `cmake --build build`。

### 批表
| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 0 | 立项 docs（本段）+ ROADMAP F5 M4 ⏳→🔄 + todo `03-timer.md` 标范围栅栏 | ✅ | `36c0f9a` | docs-only |
| 1 | ACPI HPET 表解析：`acpi.hpp` 加 `HPETHeader`+`HPETInfo`+`parse_hpet` 声明（增量，照抄 MADT 段）+ `acpi/hpet.cpp`（`parse_hpet` 纯逻辑：校验 + 解 base 地址 GAS）+ QEMU 机制测（`find_table("HPET")` 命中 + `parse_hpet` 解出 `0xFED00000`） | ✅ | `51b968e` | run-kernel-test-all 两 leg 991/0（+5 HPET 表测） |
| 2 | HPET MMIO 驱动：`hpet/hpet.{hpp,cpp}`（`g_vmm.map @+0x60000 PCD` + 读 General Capabilities 高32位取 period_fs + ENABLE_CNF 置位 + 捕获 boot counter + `monotonic_ns` 溢出安全换算，**全 32-bit 访问**）+ 机制测（period 合法 / 计数器读两次递增证 ENABLE 真生效）+ host 纯算术测（`ticks_to_ns` 溢出安全） | ✅ | `43b6ea8` | 两 leg 995/0（+4 驱动测）+ host hpet_math 5/5 |
| 3 | RTC 端口 BCD：`rtc/rtc.{hpp,cpp}`（`0x70/0x71` 索引/数据 + reg A UIP 轮询 + BCD→binary + 12/24h 模式自适应 + 世纪 reg 0x32 + `days_from_civil`→Unix epoch + boot 缓存）+ 机制测（sane 日期 + epoch∈[2024,2100)）+ host 纯算术测（bcd/epoch 已知日对照） | ✅ | `74e5bcf` | 两 leg 997/0（+2 RTC 测）+ host rtc_math 6/6；实测 `[RTC] 2026-06-30` |
| 4 | 接 `sys_clock_gettime` + 漂移修正：`do_clock_gettime_kernel` MONOTONIC←`hpet::monotonic_ns`（HPET 缺席退 PIT）、REALTIME←`rtc::boot_epoch_ns + hpet::monotonic_ns` + `main.cpp` Step11b 插 `hpet::init`+`rtc::init` + 删「无墙钟」过时注释 + 2 例 REALTIME 测 | ✅ | `3c153fc` | 两 leg 999/0（+2 clock_gettime 测）+ production boot smoke 零 panic |
| 5 | 收官：notes（每批一篇）+ ROADMAP F5 M4 ✅ + PLAN ✅。周期中断 / nanosleep / RTC 重同步 留 follow-up | ✅ | `ead047d` | docs-only |

### ✅ 收官（2026-06-30）

**CinuxOS 有了高精度 monotonic（HPET）+ 真墙钟（RTC）**：`sys_clock_gettime(CLOCK_MONOTONIC)`
走 HPET free-running 计数器（boot-relative ns），`CLOCK_REALTIME` = RTC boot epoch + HPET
monotonic delta（漂移修正）。PIT 不动（仍 BSP 抢占），仅 HPET 缺席时当 monotonic 兜底。6 commit
（B0 docs + B1-B4 实现 + B5 notes），`worktree-f5-m4-hpet-rtc` 待 PR。验证：run-kernel-test-all
两 leg **999/0**（基线 986 + 13 新测）+ host hpet_math 5/5 + rtc_math 6/6 + production boot
`[HPET]`/`[RTC]` 零 panic。详见各批 notes。

**两个 GOTCHA（接手 HPET 必读，见 [批2 note](../notes/2026-06-30-f5-m4-b2-hpet-mmio-driver.md)）**：
① General Config 寄存器在 **0x010 不是 0x008**（HPET 通用寄存器 0x10 间距；QEMU `HPET_CFG=0x010`），
写 0x008 落 reserved 静默丢弃；② period 在 Capabilities **高 32 位 [63:32]**（低 32 是 vendor/rev），
且 **QEMU 丢 64-bit MMIO 写**——全用 32-bit 访问，64-bit 计数器两半读 + rollover 保护。

**follow-up（留后续）**：HPET 周期中断（timer comparator + IRQ vector，类比 LAPIC timer，解锁
nanosleep/high-res 定时）/ RTC 周期重同步（NTP 式，需 IRQ，目前 boot 只读一次长时间会漂）。
push/PR 归用户。

### 风险 / 陷阱
- **HPET 计数器默认不跑**：spec 要求 `ENABLE_CNF`（General Config bit0）置位主计数器才递增；QEMU 遵 spec，复位态 `ENABLE_CNF=0`。故 `init` 必须写 General Config=1，否则「读两次递增」机制测必红（对齐 Linux `hpet_enable`）。
- **ns 换算溢出**：naive `ticks × period_fs / 1e6` 长期运行（年）溢出 uint64。用 hi/lo 拆分法（`ticks/1e6 × period + (ticks%1e6)×period/1e6`）—— host 单测专项守。
- **RTC 读窗口**：CMOS 更新中（reg A bit7 UIP=1）读会撕裂；UIP 轮询 + 两读一致 + **有界循环**（防挂死，测试内核全程 `cli`）。
- **REALTIME 诚实**：RTC 仅 boot 读一次，HPET 推进；无周期重同步（需 IRQ），长时间运行 HPET/RTC 会相对漂移——诚实标注，不包装成 NTP 级。
- **测试内核已就绪**：`main_test.cpp:471` 已 `acpi::init()`、`:509` 已 `g_vmm.init()`——HPET 表发现 + MMIO 映射前置都满足，无需动 harness 启动模型。
- **共享文件**：`acpi.hpp`（加 `HPETHeader` 增量，push 前全量编）、`drivers/CMakeLists.txt`、`sys_clock_gettime.cpp`、`main.cpp`、`main_test.cpp`。`KMEM_MMIO` 新槽 `+0x60000` 不动既有（AHCI@+0/LAPIC@+0x10000/IOAPIC@+0x11000/MSI-X@+0x40000/xHCI@+0x20000/e1000@+0x50000）。

## ✅ F10-M3 TTY Phase 2（PTY / 伪终端）— 收官 2026-06-30（分支 feat/f10-m3-pty 待 PR）

> Feature 域 F10 第三里程碑下半。接 Phase 1（已合 main PR#46：行规范 + 阻塞读 + ioctl TCGETS/TCSETS/TIOCGWINSZ/TIOCGPGRP/TIOCSPGRP + 信号 + ConsoleTty 类化）。**Phase 1 显式推迟到 DevFS 的部分**：PTY master/slave 对 + `/dev/ptmx` + `/dev/pts/N` + `/dev/tty` + `TIOCSCTTY` + `/dev/console` 接真 TTY。**解锁条件已满**：DevFS(PR#48) / TTY Phase 1(PR#46) / session-pgrp(F3-M3，`controlling_tty{-1}` 字段 + `setsid` 留缝)全在 main。分支 `feat/f10-m3-pty`（从干净 main `a134cb7`）。**用户决策（2026-06-30）**：全范围 B0-B5 一口气做；B2 ioctl 派发走**低风险加法**（fd>2 走 `fd→inode→ops->ioctl` 派发 + fd≤2 console fallback，行为零变）。
> 范围栅栏（不投机）：不做 TIOCSTI / 流控（IXON/IXOFF）/ select-poll（F8 epoll 的事）；PTY slave **复用现成 `TTY` 行规范**不重写；用户态真 shell 闭环留 F10-M4，Phase 2 只交**机制 + kernel 侧端到端 smoke**。
> 验证：每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg 绿（本地先 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF` 关 smoke 防挂死）；B2/B4 改公共接口（InodeOps / process_group）push 前补全量 `cmake --build build`。

### 批表
| 批 | 范围 | 状态 | 测试 |
|----|------|------|------|
| 0 | 立项 docs（本段）+ ROADMAP F10-M3 Phase2 🔄 + todo `02-tty.md` 更新 + 清 PR#45-49 文档债 | 🔄 | docs-only |
| 1 | PTY 核心纯逻辑 `pty.{hpp,cpp}`（master/slave 双 ring + slave 复用 `TTY` 行规范；master 写→slave 行规范→slave 读，slave 写→master 读，信号字符经 slave）+ host 单测 | ✅ `bab9caf` | host pty 单测 10 例 + test_host 65/65 + run-kernel-test-all 两 leg 977/0 |
| 2 | 接口缝（头号风险）：`InodeOps` 加 `ioctl()` virtual（默认 NotImplemented→-ENOTTY）+ `sys_ioctl` 改 fd→File→Inode→ops->ioctl 派发（fd≤2 console fallback 保行为零变）。`open()` virtual 推 B3 随 `/dev/ptmx` 落地 | ✅ `aadde89` | run-kernel-test-all 两 leg 977/0(零回归) + host 65/65 |
| 3 | DevFS PTY 节点：`/dev/ptmx`（open 时 `InodeOps::open` 分配一对 PTY、返 master inode）+ `/dev/pts/N`（lookup 命中 slave ops）+ master/slave InodeOps（走 PTY）+ DevFS 动态节点 | ✅ `5e50572` | run-kernel-test-all 两 leg 984/0(977+7 PTY) + host 65/65 |
| 4 | 控制终端语义：`TIOCSCTTY` 设 `controlling_tty` + 挂 PTY + `/dev/tty` 别名当前控制终端 + session 抢占语义（接 `process_group` 现成 pgid/sid）+ 负测（非 session leader 抢终端 → EPERM） | ⏳ | run-kernel-test-all 两 leg + 负测 |
| 5 | 收官：`make run` boot 冒烟（PTY DevFs 装配零 panic）+ notes + ROADMAP F10-M3 Phase2 ✅。**fork+execve-under-PTY 全闭环推迟 F10-M4**（需 `sys_dup2` 缺 + ring0 不能调带 user 指针的 syscall；busybox sh 是天然消费者） | ✅ | make run `[DEVFS] mounted at /dev (4 nodes)` 零 panic + run-kernel-test-all 986/0 两 leg |

### 风险 / 陷阱
- **B2 头号风险**：`InodeOps` 加 virtual 改公共头，牵连所有 FS 后端 mock。缓解：带默认实现（`open` 返原 inode、`ioctl` 返 -ENOTTY），旧子类零改；fd≤2 fallback 保行为零变，现有 console ioctl 测当回归网。**B2 首个动作**：查清 `sys_read fd==0` 是不是真 FDTable 项（决定 fallback 写法）。
- **PTY 无消费者**：Phase 2 是基建，真用户态 shell 闭环要等 F10-M4——诚实标注，不包装成"立刻有用户可见成果"。
- **smoke 默认 ON + 本地无 `build/musl/hello` → 挂死**（B5 本地 `CINUX_MUSL_HELLO_SMOKE=OFF`）。
- **`InodeOps` 改是公共接口**，push 前补全量编译（L5 CI 对等盲区：host mock 不在 run-kernel-test）。

## ✅ F6-M3 DevFS（/dev 设备文件系统）— 收官 2026-06-30（已合 main PR#48 8cb08f4）

> Feature 域 F6 VFS 第三里程碑。**三路并行之一**(另两路 F7-M4 UDP / F10-M2 动态链接)。**并行硬约束**:这条只做 DevFS;严格不改 `fs/inode.hpp` 的 `InodeOps` 虚函数接口(F10-M2 在用),只加 device inode 子类;不做 F6-M1 VFS 增强 / M2 ProcFS / M4 tmpfs / ext4(留 F6 后续单独收)。DevFS = 内存型虚拟 FS(无 ext2 后端,device inode 生命周期跟设备绑定):device inode(`InodeOps` 子类,封装设备驱动 ops)→ `/dev` 虚拟挂载 → 基础节点(`/dev/null` `/dev/zero` `/dev/console`,read/write 走设备)。对齐 Linux DevFS/tmpfs 的 device-inode 模式;新代码类化(`DevFs` FileSystem 子类 + 匿名 namespace 设备 ops 子类 + `CharSink` 抽象,非全局 static + 自由函数,见 memory `classify-c-style-singleton-with-mutable-state`)。F10-M3 TTY Phase2 的 PTY/`/dev/*` 依赖此。分支 `worktree-f6-m3-devfs`(worktree 从干净 main `1cdd507`)。
> 验证:每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)`(关 smoke:`cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF`)绿才提交。

### 批表
| 批 | 范围 | 状态 | 测试 |
|----|------|------|------|
| 0 | 立项 docs(本段)+ ROADMAP F6-M3 🔄 + todo `02-devfs.md` 标范围栅栏 | ✅ | docs-only |
| 1 | DevFS 核心(`devfs.hpp`/`devfs.cpp`:`DevFs`+4 设备 ops 子类+`CharSink`,纯逻辑)+ host 单测(mock sink)+ kernel 单测(`run_devfs_tests`)+ CMakeLists(fs/test) | ✅ | host 19/0 + run-kernel-test-all 两 leg 974/0(`bb7310e`) |
| 2 | boot 接线(`devfs_init.cpp` SerialConsoleSink + init.cpp 挂 `/dev`)+ `make run` 冒烟 + notes | ✅ | run-kernel-test-all 两 leg 回归 + make run `[DEVFS] mounted at /dev (3 nodes)` 零 panic(`84cd8cb`) |

### 风险/陷阱
- **smoke 默认 ON + 无 build/musl/hello → run-kernel-test 挂死**:本地一律先 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF`。
- **协调点**:三路并行唯一碰面 CMakeLists(加 source 行);init.cpp 只加一行 devfs 装配(ext2 挂载段之后),F7-M4/F10-M2 不动该段,低冲突。
- **`/dev/console` 范围诚实**:本批 write→serial 输出(基础节点);接 ConsoleTty 真 stdin/PTY 是 TTY Phase2,显式推迟不欠债。
- **设备号**:`makedev = (major<<8)|minor`(hobby OS 简化),够 `st_rdev` 上报。
- **栅栏**:不碰 [inode.hpp](../../kernel/fs/inode.hpp) 的 `InodeOps` 虚表(接口+签名一行不动);设备号收进 ops 子类 + `stat()` override 填 `st_rdev`,不加 `Inode` 字段。

## ✅ F-EXTABLE（Linux 风格 exception table 基建,SMAP follow-up #2 阶段1）— 已合 main PR#45 a44d2b8

> 横切里程碑(与 F-VERIFY/F-CLN 同档),接 SMAP saga follow-up #2。user accessor 现状靠 demand-page 契约(`user_access.hpp:19-25` 注释),无 extable:拷贝中途 fault 被 demand-page 掩盖(静默读 0 返 true)或 panic,accessor 真负测试写不了(kernel violation 必 panic)。建 RIP-based `__ex_table`(Linux uaccess 范式):fault RIP 命中注解 accessor 时 PF handler 改 `frame->rip` 跳 fixup 返 `-EFAULT`,解锁真负测试。**范围栅栏:只阶段 1(extable 基建 + accessor 重写 + 负测试),不改 demand-page 对用户态缺页的宽松逻辑**(F2 lazy-allocation 范式,撤它留 F2-M5 高风险)。分支 `feat/f-extable`(从干净 main `7f846c8`)。详见 plan `~/.claude/plans/lucky-seeking-hamster.md` + note `2026-06-29-f-extable-b1-extable-infra.md`。
> 验证:每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg 绿(基线 960/0);改公共头 push 前补全量 `cmake --build build`。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 1 | linker `__ex_table` section + `extable.hpp`(ExceptionTableEntry / extable_search 二分 / extable_sort 插入排序 / `_ASM_EXTABLE` 宏)+ main/main_test 接线 sort_extable + test_extable host 单测。**零行为变(空表)** | ✅ | `04faecd` | 两 leg 960/0 + host 7/0 + nm 空表(`__start==__stop`) |
| 2 | copy_to_user/copy_from_user 改 inline asm(rep movsb + extable + fixup clac),**不碰 PF**;put/get 保 wrapper;更新注释 | ✅ | `77ff9cf` | 两 leg 960/0 + smoke 20/20 + nm 非空(21 项)+ 反汇编两路 clac |
| 3 | handle_pf 顶部 extable 查找(内核态门 `cs&3==0` + `search_exception_tables(frame->rip)` 命中改 `frame->rip` return) | ✅ | `c09e6cf` | 两 leg 960/0 + smoke 20/0;demand-page/CoW/fork/栈守卫 全不变(查表必 miss) |
| 4 | RSVD 负测(test_user_ptr.cpp)+ host search/sort 单测 + runner 接线 + ROADMAP/PLAN/notes/memory。对照 pre-B3 证负测会 panic | ✅ | `0f60f47` | 两 leg 962/0(+2 负测 PASS)+ host 7/0 + smoke 20/20。负测实证 extable 真拦(Fallback A 未映射址,RSVD 未用) |

> 风险:accessor 回归(~30 caller,host 路径不变 + ring-3 smoke 守)/ PF 改动影响所有 #PF(查表 miss 即原逻辑)/ sort 错→search miss→panic(批1 host 单测先证)/ fixup 漏 clac→AC 泄漏(批2 反汇编审)/ RSVD 构造环境敏感(三方案降级)。批2 故意不接线 PF → accessor 回归与 #PF 改动隔离,可二分。

## ✅ F10-M3 TTY + ioctl（Phase 1 收官 2026-06-29）— 立项 2026-06-27

> 分支 `feat/f10-tty-dyn`（从干净 main `295d536`）。接 F10-M1（musl 静态移植 ✅ PR#42）。用户拍板：**先 TTY+ioctl，同分支续叠 M2 动态链接**；按需 + libc 解耦（PTY/`/dev/*` 留 F6 DevFS）。
> **前置修 `9fba65b`**：handle_pf CoW 解析松 U 位门控——内核态写 CoW 用户页（syscall 直接解引用，如 waitpid 写 `*status`）不再 panic。run-kernel-test-all 955/0 + -smp2 AP 回读 PASS，零回归。（ring-3 自动回归未成：内核写 CoW 的 mmap 页不 fault、栈页才 fault，差异未解；待 GUI `make run` 复验。）
> 验证：每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 绿才提交。

### 现状（2026-06-29 复盘修正）
**批1-3 已合 main**（经 SMAP saga `fix/smap_bug_fix` → PR#44，commit `53ef726`/`ced0066`/`35cb419`）：`sys_read fd==0` 走 `console_tty_read` cooked line 阻塞读（替旧键盘 spin）；keyboard `dispatch_key`→`console_tty_input` + kprintf 回显；console TTY 单例 + 行规范（ICANON/ECHO/ISIG + 退格/^U/^W + EOF）。**批4 `sys_ioctl` 实现 TCGETS/TCSETS/TIOCGWINSZ**（fd 0/1/2，copy_to/from_user extable SMAP 安全）→ 解锁 musl/glibc 行缓冲。**批5**：信号生成（`ConsoleTty::input` 处理 `kSignal`→`take_signal`→`killpg` 投前台组，Ctrl+C/quit/suspend）+ TIOCGPGRP/TIOCSPGRP + **console_tty 类化(ConsoleTty，私有 tty_/reader_/foreground_pgid_)**（用户反馈:有 mutable 共享状态该类化）。**剩**：批6 收尾(同类异味清理 + ROADMAP F10-M3 Phase1 ✅)。

### 批表
| 批 | 范围 | 状态 | 测试 |
|----|------|------|------|
| 0 | ✅ 立项 docs（本段）+ ROADMAP F10-M3 🔄 + todo 02-tty.md Phase 1 范围 | ✅ | docs-only |
| 1 | TTY 核心 `drivers/tty/tty.{hpp,cpp}` + termios UAPI struct + 默认 ICANON\|ECHO\|ISIG + 行规范状态机（纯逻辑，不接 read 路径，host 单测） | ✅ | host tty 9 cases + run-kernel-test（`53ef726`，经 SMAP saga 合 main） |
| 2 | 接输入源 + 回显：Keyboard `dispatch_key`→TTY `input_char` + Console 当 echo sink + 系统 console TTY 单例 | ✅ | run-kernel-test 绿（`ced0066`，经 SMAP saga 合 main） |
| 3 | stdin 阻塞读：`sys_read fd==0` 改读 TTY cooked line_buf + 无行 block（F3 `prepare_to_wait`/`schedule_blocked`）+ 键盘 IRQ 唤醒。**修 musl 误 EOF** | ✅ | run-kernel-test-all 两 leg（`35cb419`，经 SMAP saga 合 main） |
| 4 | ioctl 实命令：TCGETS/TCSETS/TIOCGWINSZon fd 0/1/2（copy_to/from_user extable SMAP 安全）+ tty.hpp ioctl UAPI + Winsize 80×25。**解锁 musl/glibc 行缓冲** | ✅ | run-kernel-test-all 964/0 两 leg + AP readback + ctest 62/0（本次） |
| 5 | 信号生成：Ctrl+C→SIGINT / Ctrl+Z→SIGTSTP / Ctrl+\\→SIGQUIT 经 killpg 投前台组 + Ctrl+D→EOF。**+ console_tty 类化(ConsoleTty)**（用户反馈:有 mutable 共享状态该类化，批6 扫同类） | ✅ | run-kernel-test-all 967/0 两 leg + AP readback + ctest 62/0（+3 测 Ctrl+C/Ctrl+Z→killpg + TIOCSPGRP） |
| 6 | 收尾：ROADMAP F10-M3 Phase1 ✅ + PLAN 收官。F-VERIFY 交织（host 镜像副本链真码）推迟留 F-VERIFY M2；全 kernel 伪单例类化评估登记独立 follow-up（13 候选文件，console_tty 批5 已类化，详见 memory `classify-c-style-singleton-with-mutable-state`）。**🚩TTY Phase1 收官：feat/f10-m3-tty 待 PR** | ✅ | docs-only（run-kernel-test-all 967/0 + ctest 62/0 已验） |

### 风险
- 批 3 阻塞读的 IRQ 唤醒（模式同 pipe write 唤醒 read，waitpid/pipe 已验）；兜底先做 cooked-buffer 非阻塞读（有行返行、无行不返假 EOF），阻塞作子步叠加。
- Phase 2（PTY / `/dev/*` 节点 / TIOCSCTTY）硬依赖 F6 DevFS，显式推迟；Phase 1 用 console TTY 单例 + `controlling_tty` 绕开，功能完整不欠债。
- 解耦：termios / TIOCGWINSZ = Linux UAPI，不出现 libc 名字；TTY 与 PT_INTERP / 动态链接正交（不缠 M2）。
- **smoke 默认 ON + 本地无 /hello → run-kernel-test 挂死**（hello_smoke 20-iter poll 循环撑满 timeout；根因 `build/musl/hello` 未 build，非 CMake target）。本地验证关 `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF`；CI 有 /hello 不受影响。
- **类化异味清理（批6 收尾）**：console_tty 批5 已类化(ConsoleTty)；批6 扫同类异味(全局 static + mutable 共享状态 + 自由函数的伪单例)评估类化。无状态 static 工具(Mouse/Keyboard)不动。

## ✅ F10-M2（ELF 动态链接 / musl ldso）— 收官 2026-06-30（已合 main PR#49 a134cb7）

> Feature 域里程碑（接 F10-M1 musl 静态移植 ✅）。worktree `worktree-f10-m2-dynlink`（从干净 main `1cdd507`，三路并行之一：另两路 F7-M4 UDP / F6-M3 DevFS；本路碰 `kernel/proc/execve.cpp` + elf_loader + 用户态 loader，与 F7 零冲突，与 F6 只共享 VFS `inode->ops->read` 接口——只用不改，零冲突）。**用户决策**：按需建动态链接（对齐 Linux，不重造）；**不自建动态 loader**——用 musl 自带的 ld-musl(ldso)做符号解析/GOT 填充；libc 解耦(PT_INTERP 是 musl/glibc 切换缝，不写死 libc 名)；ELF base ASLR / PIE 主程序是 follow-up，先 non-PIE 动态跑通。F10-M3 TTY / fork saga 不碰（已合 main）。
> 验证：每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg 绿才提交；批2 host 工具链产物 readelf 验；批3 加 dyn smoke(关本地默认)端到端。

### 调研实证（决定打法，已拉 musl-1.2.5 源码核实，不猜）
- **interp 路径串** = `/lib/ld-musl-x86_64.so.1`（musl Makefile `LDSO_PATHNAME = $(syslibdir)/ld-musl-$(ARCH)$(SUBARCH).so.1`；x86_64 + 空 SUBARCH）。`make install` 产 `lib/libc.so`(ldso 本体)+ 装 symlink → 动态 hello 的 PT_INTERP 就指这个路径。
- **shared 默认开**（configure `--disable-shared [enabled]`）→ 现有 `build-musl.sh` 已自带 `libc.so`，批2 只需新写 hello-dyn，不改 musl 构建。
- **ldso(ET_DYN)靠 `__ehdr_start` 定位自身**（`ldso/dynlink.c`），kernel 只需把它在某个 base 连续映上去；AT_BASE 给了更稳（fduc 只有 fdpic 用）。
- **ldso 读的 auxv 契约**：`AT_PHDR/PHNUM/PHENT`(主程序 phdr VA —— ldso 用 `AT_PHDR - PT_PHDR.p_vaddr` 算主程序 base)、`AT_ENTRY`(主程序 entry —— 重定位完 `CRTJUMP(aux[AT_ENTRY])` 跳过去)、`AT_BASE`(ldso base)、`AT_PAGESZ`、`AT_UID/EUID/GID/EGID/AT_SECURE`、`AT_HWCAP`、`AT_EXECFN`、`AT_RANDOM`，vDSO 可选。
- **ELF 常量**：PT_DYNAMIC=2 / PT_INTERP=3 / PT_PHDR=6（musl `include/elf.h`）。interp(ldso)=ET_DYN=3；主程序动态 non-PIE=ET_EXEC=2。
- **结论（kernel 做 / 不做）**：kernel 只做 ① 主程序 phdr 扫 PT_INTERP→读 interp 路径 ② interp ELF 当 ET_DYN 映到 base ③ entry 改 interp 入口、auxv 喂 AT_BASE/AT_ENTRY/AT_PHDR。**GOT/PLT 重定位、DT_NEEDED、符号解析全由 musl ldso 在用户态干，kernel 不碰。**

### 批表
| 批 | 范围 | 状态 | 测试 |
|----|------|------|------|
| 0 | 立项 docs（本段）+ ROADMAP F10 M2 ⏳→🔄 + 重写 todo `f10-userspace/01-elf-dynamic.md`（从旧"自建 ld-cinux"改成"用 musl ldso + kernel 只做 PT_INTERP/interp/auxv"） | ✅ | docs-only |
| 1 | kernel 核心：`elf_types`{+ET_DYN/PT_INTERP/PT_DYNAMIC/PT_PHDR} + `validate` 收 ET_EXEC∨ET_DYN(+ET_DYN 单测) + `memory_layout`{+USER_INTERP_BASE=256MB} + `ElfAuxInfo`{+at_base,has_interp} + 新 `elf_load.{hpp,cpp}`:`load_elf_image`(主+interp 共用 PT_LOAD 映射)+ `load_interpreter`(resolve+读+映) + execve 扫 PT_INTERP→加载 interp→entry=interp 入口 + `enter_loaded_program` 发 AT_BASE。**静态路径行为不变** | ✅ `b55615e` | 单核 968/0(+1 ET_DYN)+ -smp2 ALL PASSED + AP readback + host 54/54。零静态回归 |
| 2 | 工具链：`tools/musl/build-hello-dyn.sh`(-no-pie + `-dynamic-linker /lib/ld-musl-x86_64.so.1`，产 `build/musl/hello-dyn`，readelf 验 ET_EXEC+PT_INTERP+PT_PHDR+DYNAMIC) + README 补动态说明 + `create_ext2_disk.sh`{+hello-dyn + ldso 两可选 arg；`mkdir lib`+`write libc.so lib/ld-musl-x86_64.so.1`} + `qemu.cmake` 穿 artifact(条件 include) | ✅ `33e6bfe` | readelf 段/interp 正确 + debugfs 确认 ext2 装 /hello-dyn + /lib/ld-musl-x86_64.so.1(CMake 集成两路) |
| 3 | 端到端 smoke + 收官：`main_test.cpp` smoke entry 加 /hello-dyn fork+execve+waitpid 相(`CINUX_MUSL_DYN_SMOKE` gate 默认 OFF；harness 外层 gate 改任一开则编，静态/动态相独立 gate)+ `ap_test_selfcheck`/注册段 gate 同步；**ext2 image 改 4096-byte 块**(避 double-indirect 截断，interp 822KB 落进 single-indirect)；notes + ROADMAP/PLAN F10-M2 ✅ | ✅ | dyn smoke ON：单核 + -smp2 两 leg 968/0 + 串口 5× `Hello from musl on CinuxOS!` + interp base=0x10000000 + hello-dyn 5/5 PASS；默认 OFF 仍绿 |

### 风险 / 陷阱
- **批1 抽 helper** 是改启动核心路径(execve)：保静态路径行为不变（无 PT_INTERP 走原路），用既有 musl 静态 smoke + ET_DYN 单测兜回归。
- **interp 是 ET_DYN**：映到 USER_INTERP_BASE(base + p_vaddr)；主程序 non-PIE 仍绝对地址。validate 放宽收 ET_DYN 为 interp 必需（顺带铺 PIE 主程序的路，留 follow-up）。
- **interp 必须装进 ext2 的 `/lib/ld-musl-x86_64.so.1`**（PT_INTERP 指定路径），否则 execve 找不到。
- **smoke 默认 ON + 本地无 `build/musl/hello-dyn` → 挂死**，本地验证关 `-DCINUX_MUSL_DYN_SMOKE=OFF`（同 F10-M1 hello smoke 惯例）。
- **musl-gcc wrapper 在 GCC≥14 坏**（README gotcha #2），hello-dyn 也手动 `-nostdlib` 链。
- auxv 的 AT_PHDR 必须是主程序 phdr VA（现状 execve 已算 `phdr_va`，对动态主程序也成立——ldso 据它+PT_PHDR 算 base）。

### ✅ 收官（2026-06-30）
**CinuxOS 能跑 musl 动态用户程序**：execve 识别 PT_INTERP → 加载 interp(ldso)到 USER_INTERP_BASE → entry=interp 入口、auxv 喂 AT_BASE/AT_ENTRY/AT_PHDR → musl ldso 在用户态做 GOT/PLT 重定位 + DT_NEEDED + 符号解析 → 跳 AT_ENTRY → write() 经 libc.so 输出。4 commit（批0 docs `bcfebf8` / 批1 kernel `b55615e`+note `6c4309d` / 批2 toolchain `33e6bfe` / 批3 smoke+收官），`worktree-f10-m2-dynlink` 待 push。验证：dyn smoke ON 单核 + -smp2 两 leg 968/0 + 串口 `Hello from musl on CinuxOS!` ×5 + hello-dyn 5/5 PASS；默认 OFF 零回归。详见 notes `2026-06-30-f10-m2-b{1,2,3}-*.md`。

**follow-up（批3 发现 + 历史）**：
- **ext2 double-indirect / triple-indirect 缺失**：`ext2_common.cpp` 块映射只处理 direct + single-indirect，>268KB(1024 块)/>4MB(4096 块)文件读会截断。批3 把 QEMU ext2 image 改 4096-byte 块规避（interp 822KB 落进 single-indirect 4MB 上限）。**✅ 真修完成（顶部「F10-M2 follow-up ext2 double-indirect」段，分支 `worktree-ext2-indirect` 待 PR）**：补 double-indirect(i_block[13]) 读+写 + 撤 4096 workaround（盘改回 1024）；triple(slot 14) 不做。
- ELF base ASLR / PIE 主程序（批1 ET_DYN 接受已铺路，R_X86_64_RELATIVE 重定位留后）+ interp base ASLR（现固定 USER_INTERP_BASE）。
- glibc 动态二进制（PT_INTERP=/lib64/ld-linux-x86-64.so.2 天然支持，按需验）。

## 🔄 F-VERIFY（动态验证与并发检测基建）— 2026-06-27 立项

> 横切里程碑（与 FO/F-INFRA/F-QA/F-CLN 同档）。**起源**：2026-06-27 audit（15-agent workflow 抽 165 调试时间坑 + 审 5 维基建）结论——2026-06-21 那轮 14 维静态债务审计量的是「代码写得对不对」，但所有超时调试都是「代码有没有真被跑到、机制有没有真生效、崩了能不能一眼看懂」（动态/环境性另一根轴，静态债表天生瞎）。165 坑根因 top：机制没真生效 27 / 并发没压到 22 / 基建≠生产 19 / 规格看错 17 / 内存 UAF 14。**目标**：把多会话 forensics 级调试变一次 CI 红灯。**用户决策（2026-06-27）**：全做 M0-M6，重锤 SMP+并发；并补**测试矩阵盘点**（现在测试太乱、没人知道到底覆盖了什么）+**测试代码整理**（框架 bug / 共享 util / 0x1234 假 CoW / 镜像副本）。先改 ROADMAP/PLAN 立项，等确认再开 feat 分支。详见 audit memory `debugging-audit-dynamic-coverage-gap`。

### 调研结论（决定打法，已读码 + workflow 核实）
- **run-kernel-test-smp 是空转 SMP 门**：[main_test.cpp](../../kernel/test/main_test.cpp) 全程不调 `boot_aps()`、不 `sti`、worker 无 AddressSpace；唯一的 `smp.hpp` 引用只借 `kLapicTimerVector` 给 e1000 定时器。`-smp 2` 起来但套件 BSP-only 跑 → SMP 门通过=没通过。
- **fork/CoW 在测试里是死代码**：[test_clone.cpp:142](../../kernel/test/test_clone.cpp#L142) 用 `reinterpret_cast<AddressSpace*>(0x1234)` 哨兵，不建带用户页表的真 AS；[main_test.cpp:189](../../kernel/test/main_test.cpp#L189) 那个 `new AddressSpace()` 是 ring-3 smoke 临跑前装的全新 AS，从不走继承父 AS 的 CoW 页表拷贝。→ F10 shell fork/CoW 那类 bug CI 永远抓不到（现靠 headless forktest 装 /hello 绕过）。
- **全仓零数据竞争检测器**：`CINUX_HOST_TSAN` 在 [test/CMakeLists.txt:22](../../test/CMakeLists.txt#L22) 只是注释，无 `option()`；lockdep（F4-M5）只看锁序 AB-BA，看不见「两核裸碰一个字段」这种原始数据竞争——这正是 CoW 跨核 UAF / registry·pid 锁 / 迁移竞态的盲区。
- **RUN_ALL_TESTS 虚报 PASS**：[test_framework.h](../../test/framework/test_framework.h) 逐项永远打 `[PASS]`+`_tests_passed++`，即使 ASSERT 失败（失败计数也 +1，但 PASS 计数虚高、逐项 PASS 行不可信）。M0 先修。
- **机制回读只有单点**：仅 `test_usermode::test_f9` 读 CR4 SMEP/SMAP（BSP 单核）；AP 侧 LSTAR/EFER/STAR/SFMASK/CR4.OSFXSR 零回读——这是 SMEP/SMAP「4 批 931/0 假绿」的同根。
- **F10 四修已合 main**（PR#42 `b31d65e`，`ITERS=2` 单 CPU + `-smp 2` 均 races=0、954/0）：CoW TLB flush / syscall frame 128B callee-saved / free_subtree mapcount / copied-stack RBP 重定位。**但都是 forensics 修的、无 harness 测试守**——M5 的价值 = 建回归网 + 在 -smp 真压力下复验，防下次回归又变多会话排查。

### 批表
| 批 | 范围 | 状态 | 测试 |
|----|------|------|------|
| 0 | ✅ 零风险快速赢点（核实后定稿 + 落地）：M0-1 修 RUN_ALL_TESTS 虚报（快照 `_tests_failed` 前后比对） / M0-3 test_f9 扩 OSFXSR·OSXMMEXCPT 回读 / M0-4 清扫 22 处 `0x%p` 双前缀（big+mini `%p` 均自带 `0x`）+ G7 grep-a·`0x%p` 约定 / M0-5 check_memory_layout.py 接 CI 静态重叠门（PCI BAR 运行时分配，留 M3/M6 自测）。~~M0-2 `-fsanitize=unsigned-integer-overflow`~~ **GCC 不支持（Clang-only，编译器 `unrecognized` 报错），核实后砍** —— unsigned-wrap 防护改走 execve 显式 checked 运算，移入 DEBT-020/012 债修批（非零风险，不属 M0）。~~`kMaxCpus` static_assert~~ 已存在 [ap_main.cpp:35](../../kernel/arch/x86_64/ap_main.cpp#L35)（核实后砍） | ✅ | run-kernel-test 954/0 + test_host 60/0 + CINUX_UBSAN build 954/0 零命中；commits 4720138 / a900a7f / 73944b4 |
| 1 | ✅ **测试矩阵盘点**：6-agent workflow 审 **47 子系统 × 6 维度 = 282 格**全量 grep 坐实；**36 假测 + 38 机制位**登记。三大结构性盲区量化坐实：**host-integration 真码链接 37/47 ❌**（镜像副本：test_pmm/vmm/address_space/scheduler 重实现逻辑；`add_cinux_integration_test(vmm)` 只链 unit 不链真 vmm.cpp —— 16 个 integration 中部分名不副实）、**QEMU-SMP 47/47 ❌（空转）**、**机制回读 ~27/47 ❌ + AP 侧零回读**。矩阵 [test-matrix.md](../todo/quality/test-matrix.md) 成为 F-VERIFY 追踪表（平行 debt.md）。 | ✅ | docs-only（grep 坐实 + 抽检 test_pmm「reimplemented」/ vmm 链码 / fork_exec 链码 确认） |
| 2 | **测试代码整理 + 共享 util**：`CurrentTaskGuard` / 标准化 `current`+`AddressSpace`+`MockPMM` mocking util / 拆 flat [main_test.cpp](../../kernel/test/main_test.cpp) 按域分组 / 镜像副本改链接真码（依赖轻的 fork·clone CoW 标记、scheduler enqueue·pick、execve 映射）/ DIRECTIVES 记 harness↔生产 bring-up parity 清单 | ⏳ | run-kernel-test 绿（重构不改语义） |
| 3 | ✅ **SMP 测试唤醒基建**（enabler）：M3-1 测试内核加真 `acpi::init()`（-smp 2 检出 2 CPU）+ M3-2 生产侧 **gated 钩子**（[smp.hpp](../../kernel/arch/x86_64/smp.hpp) `g_ap_test_selfcheck_fn`，fn=null 生产字节不变）+ 测试内核 `boot_aps()` + AP 跑 CR4/EFER/LSTAR/STAR/SFMASK 回读 → BSP 轮询 magic 断言（lstar!=0 / CR4 OSFXSR·OSXMMEXCPT / EFER.NXE）。**`run-kernel-test-smp` 不再空转**：AP 真唤醒 + AP 侧机制回读 PASS（cr4=0x300620 SMEP\|SMAP\|OSFXSR、lstar=&syscall_entry 非零、efer=0xd01 NXE）。LSTAR==0 #DF 类（F5-M5 GOTCHA）现 CI 立抓。**跨核真任务压力**（需 scheduler）折入 M5。 | ✅ | run-kernel-test-all 两 leg 945/0+954/0；-smp AP1 online+回读 PASS（141b093+03201e8） |
| 4 | 🔄 **并发检测基建**：✅ host `CINUX_HOST_TSAN` option + CI job（`c76f98e`，60/60 零 race；补 yield 修 test_sync_concurrent TSAN 病理）／ ⏳ 内核 KCSAN-lite（opt-in `CINUX_KCSAN`，已知 TOCTOU 点重读热字段→kpanic）— 补 lockdep 看不见的原始数据竞争 | 🔄 | host TSAN ✅；KCSAN-lite 待做 |
| 5 | ✅ **真用户 fork/CoW 压力回归**：M5-1 真 `copy_page_table_level` CoW 标记测试（`896f93d`，消 M1 头号假测；挖出 level 语义坑已修）+ M5-2a `-smp forktest` CoW 压力门（`c398ffe`，真用户 fork+CoW 50 迭代 races=0）+ M5-2b 跨核 CoW（`46fe2fa`，ApSelfcheckFn 返 bool→AP 进调度器，forktest child 跨核跑，races=0）。**F10 四修在 -smp 真跨核压力下稳，未挖出跨核 UAF。** | ✅ | run-kernel-test-smp（smoke ON）forktest races=0；默认 gate（smoke OFF）955/0 零回归 |
| 6 | 🔄 **故障可观测增强**：✅ M6-1 #PF debugcon 首故障捕获+err 解码（`b08a135`）+ ✅ M6-2 CoW 解析失败 dump phys+mapcount（lock-free PTE 走读，`6123905`）／ ⏳ 用户栈 hexdump（SMAP 需 stac/clac 包）+ page-owner tag | 🔄 | M6-1/M6-2 ✅；用户栈 dump/page-owner 待做 |

### 风险重点
- **M3 技术最硬**：测试内核镜像 SMP bring-up 不引入生产依赖，需重构 harness 启动模型（GOTCHA 候选：test-worker per-CPU 栈/GS/中断；AP worker 不能干扰 943 个现有测试，全程 `cli` 默认、局部 `sti`）。
- **M2 镜像副本改链接真码**依赖提取非平凡（fork/scheduler/execve 牵连多），可能拆 2a/2b；**M5 依赖 M3 的 SMP 真能跑**。
- **M4 KCSAN-lite 误报**：只插桩已知 TOCTOU 点（Task state/refcount/registry/pid/mapcount），不全局插桩，否则噪声淹没信号。
- **M5 会主动暴露回归**：负测试设计好（撤掉任一 F10 修必红），证明测试真有效而非又一张假绿——这正是 F-VERIFY 的精神。
- 架构契合：A.6 无异常/RTTI（KCSAN-lite 走 kpanic 不走异常）；opt-in（`CINUX_HOST_TSAN`/`CINUX_KCSAN`/`CINUX_PAGE_OWNER`）不污染默认构建性能/确定性；对齐 Linux（机制回读矩阵、数据竞争检测 CONFIG_KCSAN）。
- 建议顺序：M0 → M1（摸清家底）→ M2（理地基）→ M3（SMP enabler）→ M5（回归网，headline）→ M4 ‖ M6（独立收口）。

## 🔄 F10-M1（用户态运行时 / musl 静态移植）— 2026-06-26 立项

> Feature 域大弧。F9 安全地基已就位（NX/SMEP/SMAP + ASLR + 凭证 + Canary 全完成并合 main PR#38）。**方向决策（用户 2026-06-26）**：**不自建 libc 生态**——`user/libc`（syscall/string/printf 三件套）仅作 QEMU 测试壳保留，不扩；**直接移植 musl 作唯一 libc**。先自己源码编译 musl（自包含 sysroot），跑通后切 **musl-gcc** 当工具链驱动（上层 userland 一律 musl-gcc 编）。砍掉旧 M1「自建 libc 扩 80 syscall」（造轮子无意义）。旧 M5「musl+glibc」内容并入本 M1；glibc 静态兼容降为可选 stretch（比 musl 重得多）。分支 `feat/f10-musl`（从干净 main `fb25c89`）。
> 验证：每批 `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` 绿才提交；批1/2/4 改公共 syscall 接口补全量 `cmake --build build`；批6 加 `timeout 40 cmake --build build --target run` 冒烟（musl hello world 真输出）。

### 调研结论（决定打法，已读码核实）
- **syscall 返回约定基本已是 Linux 风格**：`sys_open` 返 `-to_errno(...)`（[sys_open.cpp:42](../../kernel/syscall/sys_open.cpp#L42)，`kernel/errno.hpp::to_errno`），即 musl 要的"负 errno"。仅几个老 syscall 返裸 `-1`（`sys_getpid:20` / `sys_getppid:17` / `sys_pipe:56,86,98`），批1 收尾改 `-errno`。
- **execve 未压 auxv 辅助向量**（grep 全空）—— **musl 头号拦路虎**。musl `__init_libc` 启动必从栈读 `AT_PHDR/AT_PHNUM/AT_PAGESZ/AT_RANDOM/AT_ENTRY...`，没有直接崩。批3 补（向后兼容：老程序读不到 auxv 无害）。
- **`SYS_chdir=12` 与 `SYS_brk=12` 撞号**（[syscall_nums.hpp:28,48](../../kernel/syscall/syscall_nums.hpp#L28)），brk 后注册覆盖 chdir → **chdir 当前是坏的**。Linux 正确号 chdir=80、brk=12。批1 全表对齐 Linux x86_64 号顺手修。
- **musl 走 `*at` 家族**（`openat=257` / `newfstatat=262`），不调老 `open/stat`。故"补 syscall"= 补 musl 真正会发的那 ~25 个，非旧规划的 80。
- 现有 ELF 加载：`kernel/proc/execve.cpp` + `kernel/mini/elf_loader.cpp` 在用（加载测试 shell）；musl 静态 ELF 走同路径，唯一缺口是 auxv。

### 批表
| 批 | 范围 | 状态 | 测试 |
|----|------|------|------|
| 0 | 立项 docs（本段）+ ROADMAP F10 🔄 + 清 PLAN 顶 F9/F5-M6/F-GUI-DECOUPLE 三段 🔄→✅ + 重写 todo/f10-userspace/00-libc.md（musl-first 策略 + ABI 差距清单）| ✅ | docs-only |
| 1 | syscall 号纠偏（chdir 12→80，全表对齐 Linux x86_64）+ 返回约定收尾（getpid/getppid/getcwd/pipe `-1`→`-errno`）+ sys_pipe RAII 化（`std::unique_ptr`，用户授权 scope 加）| ✅ | run-kernel-test 945/0 + chdir 回归 + 全量绿（`7a9f1e6`）|
| 2 | Linux 结构体布局：`sys_stat` 改 Linux 144B（顺序/pad0/`*_nsec`/`__unused`）+ `UserSigAction` 重排 `{handler,flags,restorer,mask}` + sa_flags 接入；sigset 已 8B 合规；iovec 留批4 | ✅ | g++ 探针逐字段对齐 Linux UAPI + run-kernel-test 945/0（`40be22e`）|
| 3 | execve/launch 压 Linux 初始栈（argc/argv/envp/auxv）：纯函数 `build_initial_stack`（host 单测）+ execve `ElfAuxInfo` out-param + launch_user_program 直接铺栈页（AT_PHDR/PHNUM/PHENT/PAGESZ/ENTRY/UID…/SECURE/RANDOM，rsp%16==0）。sys_execve 替换路径栈铺设留 follow-up | ✅ | helper host 单测 3/3 + run-kernel-test 945/0 + make run 无崩；end-to-end 留批6（`20baf76`）|
| 4 | 补 musl 所需 syscall：openat/newfstatat/close/read/write/exit_group/mmap/munmap/mprotect/brk/lseek/getpid/getuid…/futex/rt_sig*/clone/wait4（装不下拆 4a/4b）| ✅ | run-kernel-test 950/0(+5) + 全量绿 + test_host 55/0（`8bab7a2`）|
| 5 | musl 源码编译 + sysroot（configure+make → libc.a/crt1.o），自包含；`-static` 编 hello world | ✅ | tools/musl/ 脚本端到端可复现：build-musl.sh→build-hello.sh→hello 输出+ELF ET_EXEC@0x400000（`ea20c27`）|
| 6 | 端到端：musl hello world 经 execve+ELF loader+auxv 在 QEMU 跑通，printf 输出；加测试项；notes | ✅ | harness ring-3 smoke（`CINUX_MUSL_HELLO_SMOKE`）：950 单测 + 串口 `Hello from musl on CinuxOS!`（write 路径）+ exit 0 + smoke PASS；默认 OFF 950/0。printf/stdout FILE segfault 留 follow-up（`aad9736`）|

### 风险重点
- **批3 auxv**（碰程序启动核心路径，保现有 shell 不崩）；**批5 musl 源码编译**（host 工具链依赖，最大外部不确定项）；批1 改号是 ABI 破坏性，碰 `user/libc/syscall.h` + shell，全量回归。
- 架构契合：A.6 禁异常/ErrorOr（内核侧 ErrorOr，仅 trap 入口翻 -errno，批1/4 强化翻译边界）；A 子模块边界（musl 外部库/独立 sysroot，不进 kernel/）；对齐 Linux（musl 强制全树 Linux ABI，顺清 chdir 撞号等历史债）。

### 🔄 F10-M1 follow-up（shell 启动 musl + SMP fork）— 多类根因全修（含 2026-06-29 复活 saga），待 GUI 交互复验

> 接批6：GUI shell 敲 /path 启动 musl 程序（fork+execve+waitpid，标准 shell fallback）。**多类根因**：
> 1. **SMP CoW 写穿透竞态**（主因）：`copy_page_table_level`（fork.cpp/clone.cpp 共用）把**父进程在用 PTE** 改 writable→CoW 后**全程不刷 TLB** → 父 CPU 缓存陈旧 writable 项 → fork 返回用户态后父写穿透到共享物理页 → 污染子的 CoW 副本。单 CPU 稳（下次 context_switch 重载 cr3 刷 TLB）；-smp 2 父不切地址空间→陈旧 TLB 一直在→炸。6-agent workflow 对抗确认（mapcount 已原子；handle_cow_fault 对 fork 正确，无需改）。
> 2. **syscall_entry sysretq 不恢复完整 callee-saved 用户寄存器**（pre-existing ABI 违规，复现器撞出）：早期只恢复 RBX，先撞出 RBP(frame+88) 漏恢复；第二关 GUI shell 又撞出 R12-R15 漏保存/恢复。shell 编译后把 `argv[0]` 缓存在 R12，fork child 从 copied kernel stack/Task::ctx 恢复后 R12 陈旧为 0，导致 `execve path=0x0`、exit 127，看起来像 `/hello` 不打印但其实没 exec 成功。修为 syscall frame 96B→128B，保存/恢复 R12-R15+RBX+RBP。
> 2b. **syscall_entry 仍漏恢复 Linux syscall-preserved 参数寄存器**（2026-07-01 F-ECO O2 follow-up）：musl inline `syscall` 只声明 `rax/rcx/r11` 变化，假设 `rdi/rsi/rdx/r10/r8/r9` 跨 syscall 保持；CinuxOS 入口已保存这些寄存器，但返回路径只恢复 callee-saved，导致 `syscall_dispatch` 的 C 调用 clobber 泄漏回用户态，`-O1/-O2` musl/busybox 复用脏寄存器后堆元数据损坏，最终 mallocng `a_crash` `hlt`。修为 `syscall_entry` + `ret_from_fork` 从 128B frame 恢复 `rdi/rsi/rdx/r10/r8/r9/r12-r15/rbx/rbp`，`rax` 仍为返回值、`rcx/r11` 保持 SYSRET clobber。详见 [note](../notes/2026-07-01-syscall-abi-preserve-user-registers.md)。
> 2c. **syscall/ISR 未保护用户 SIMD/FPU 状态**（2026-07-01 F-ECO O2 真根因）：musl mallocng 在 `__malloc_alloc_meta` 里用 `pxor %xmm0,%xmm0` 生成零向量，跨 syscall/缺页后再 `movups %xmm0,(meta)` 清 `prev/next`；CinuxOS 进入 kernel C/C++ 前只保存 GPR，允许内核日志/编译器 clobber 用户 `xmm0`，于是 malloc 元数据被写入格式化日志残留并在 `a_crash @ 0x406A66` 触发 `hlt`。修为 syscall 与 ISR 入口先 `fxsave` 用户/被中断 SIMD 到栈上，返回前 `fxrstor`，同时刷新 `Task::fpu_state` 供 fork/preemption 使用；`ret_from_fork` 恢复 child 继承的 SIMD 状态。另补匿名 demand page zero-fill，避免 brk/MAP_ANON/stack 泄漏旧页内容。实测真实 QEMU busybox `echo 5/5 PASS` + `ls PASS`。详见 [note](../notes/2026-07-01-syscall-simd-state-preserve.md)。
> 3. **AddressSpace 析构误释放 CoW 共享叶子页**（本次）：`AddressSpace::free_subtree()` 注释说 PT 层不释放 data page，但代码在循环尾部仍把叶子 PTE `phys_addr()` 当页表页 `free_page()`；fork child 被 waitpid reap 后释放了 parent 仍映射的 text/data/stack 物理页，parent 第二轮 fork 继续执行的 `0x401182` 代码页变成全 0（实际执行 `00 00`，fault addr=`rax`=pid=0xb）。修为 PT 叶子按 mapcount `dec_and_test`，最后映射才 free；中间层递归释放页表页。
> 4. **fork/clone 拷贝内核栈后未重定位 saved RBP 链**（GUI `RIP=0xCCCC...`）：`ctx.rsp` 被平移到 child kernel stack，但 copied frames 里的 saved RBP 仍指 parent stack；child 经 `fork_child_trampoline` 返回 `sys_fork` 后，`syscall_dispatch` 的 `leave; ret` 沿 parent stack 退栈，最终 ret 到填充值 `0xCCCC...`。修为 fork/clone 共用 `prepare_copied_kernel_stack_context()`，沿实际复制的 RBP chain 把 saved RBP 重写到 child stack。
> 5. **跨核 exit/reap「复活」UAF**（2026-06-29，最深、由 SMAP 收尾 follow-up #1「-smp2 smoke 假绿」引出，前 4 类皆必要但不足以解释 -smp2 ring-3 smoke 的反复 `sys_exit(垃圾)`）：child 在 AP 上 `sys_exit` 设 Zombie 后 `yield`，`schedule()` 该切到 idle，但 `Scheduler::init()`（smoke bootstrap 调）把 `idle_tasks_` 全置空后**只重建 BSP 的**，AP 的 `idle_tasks_[1]` 丢失 → `idle()` 返 nullptr → schedule 队列空+无 idle → 早返回 → Zombie 没被切走 → sys_exit 返回 → 回用户态循环 `exit_group(垃圾)`（每 child 重入 ~400 次，reap+free 后仍重入 = 复活 UAF）。诊断前提是 **kprintf SMP 串行化**（`ea816dc`，并发 kprintf 逐字节 `Serial::putc` 无锁致日志全乱）。**修（`24c4559`）**：① `init()` 为 online AP 重建 idle（`smp.hpp online_ap_count()` + `scheduler.cpp` 调 `setup_ap_idle`）—— 复活根因；② `waitpid` reap 前等 `on_cpu==-1`（`context_switch.S` 存完 prev ctx 的硬同步点）—— 治 free-during-switch；③ `add_task` 加 `wake_ap` 旋钮；④ smoke bootstrap `run_first` 返回=BSP `cli;hlt` park 等 AP 的 outl（治 did-not-run 假绿/假红）。详见 [复活 saga note](../notes/2026-06-29-smp-fork-reap-resurrection-fix.md)。
>
> **修**：fork.cpp/clone.cpp CoW 遍历后 `flush_tlb_all()`（局部刷足够：fork IF=0 父不可迁移，子 cr3 全新 clean）；syscall.S frame 扩到 128B 并保存/恢复 R12-R15+RBX+RBP；address_space.cpp `free_subtree()` PT 叶子按 mapcount 释放；fork/clone copied kernel stack RBP chain 重定位；clone user-RSP patch frame size 同步 128B。清 syscall.cpp 的 `[FORK] dispatch` 诊断打印；本次临时 syscall/PF trace 已撤。
> **复现器** `tools/musl/forktest.c`（裸 SYS_fork(57) 循环 = shell 精确路径；父 post-fork 写共享 CoW 全局、子读；races 计数走串口；装成 /hello 让 ring-3 smoke 在 -smp 2 起）。**实证**：`ITERS=2` 单 CPU + `-smp 2` 均 `FORKTEST iters=2 races=0 clean=2 errs=0`；普通 musl hello `-smp 2` 输出 `Hello from musl on CinuxOS!` 且 smoke PASS；新增 fork/clone copied RBP chain 单测；headless 单 CPU + `-smp 2` test image 均 `954 passed, 0 failed`。当前 sandbox 不能开 CMake target 的 `-vnc :0`，用同一 CMake 产物 `-display none` 直跑 QEMU；GUI shell 交互仍需本机可视环境复验。详见 [AddressSpace note](../notes/2026-06-27-f10-address-space-free-subtree-cow-fix.md) + [fork stack note](../notes/2026-06-27-f10-fork-kernel-stack-rbp-relocation-fix.md)。

## ✅ F9 安全机制（NX/SMEP/SMAP + ASLR + UID/GID + Canary）— 收官 2026-06-26（PR#38 d06c842，942/0）

> Feature 域里程碑（开 F10 用户态运行时大弧前）。目标：CPU 硬件级保护（NX/SMEP/SMAP）+ 地址随机化（ASLR）+ 进程凭证（UID/GID）+ 栈溢出保护（Canary），为 F10 libc/ELF/TTY 铺安全地基（**F9 NXE 是 F10 硬前置**）。来源：用户决策「干掉 F9」+ memory `f4-current-focus`（F9 为 F10 前置）。范围：用户拍板 **M1-M4 全做**（文件权限 check_permission 与 F6 VFS 强耦合，仍建议留 F6）。分支 `feat/f9-security`（从干净 main `8be32d4`）。
> 验证：每批 `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` 绿才提交；NXE/SMAP 批加 `make run` 冒烟（用户程序真能跑、不因 NX/SMAP 崩）；SMAP 批加 DEBT-019 重审。

### 调研结论（决定打法，已读码核实）
- **EFER.NXE 总开关当前未开**：`usermode.S` 设 EFER 只 `orq $1`（SCE），无 NXE（bit 11）；mini 长模式入口亦未设。`paging_config.hpp` 早有 `FLAG_NX=1<<63`，`execve.cpp:256` 已给非 PF_X 段设 NX，但 NXE 关时 bit 63 是保留位（矛盾，批2 查清）。五处代码（signal/address_space/sys_mmap/execve/exception_handlers）注释里等 NXE。
- **sigreturn 是 NXE 开闸的头号拦路虎**：`signal.cpp:354-358` 把 8 字节 `int $0x80` trampoline **写在用户栈上**，handler ret 落此执行进 sigreturn gate（vector 0x80）。注释明说「依赖用户栈可执行（NXE off until F9）」。开 NXE + 栈标 NX 后必崩 → **批1 先把 sigreturn vDSO 化**（固定用户可执行页 @0x100000，对齐 Linux `__restore_rt`），再开闸。
- **SMAP 是 M1 硬骨头（非 SMEP）**：内核无 copy_from_user，只 `validate_user_ptr` 校验地址 + syscall 直接解引用用户内存。SMAP 开后这些直解引用全要 stac/clac 护（照 Linux：syscall entry stac 一次，handler 全程放行，exit clac）。撞 DEBT-019（用户指针 PF 兜底），批4 重审。
- **SMEP 低风险**：内核经 sysretq/iretq 进用户态，不直接执行用户页。
- **VmaFlags::Exec 已有**（execve.cpp:318 设）：批2 demand-read 判 NX 可用。地址布局：ELF@0x400000 / heap≤0x4000000 / mmap 4–24GB / 栈顶 0x7FFFFF000。

### 批表（M1 为主，M2-M4 随后）
| 批 | 范围 | 状态 | 测试 |
|----|------|------|------|
| 0 | 立项 docs（本段）+ ROADMAP F9 状态 + 调研结论 | ✅ | docs-only |
| 1 | sigreturn vDSO 化（固定可执行页，脱离栈上代码，行为不变）| ✅ | run-kernel-test 931/0 + 信号 round-trip 不变(`4a16158`) |
| 2 | 开 EFER.NXE（x86_64 baseline，无 CPUID gate）+ 补 NX（PF handler 按 VMA Exec：mmap/demand-read/栈/heap）+ 清 deferred 注释 | ✅ | run-kernel-test 931/0 + make run GUI/shell/xHCI 冒烟零 panic（(本次)） |
| 3 | SMEP：CR4.SMEP（CPUID.07H:EBX[7] 检测）+ BSP/AP per-CPU 设 | ✅ | run-kernel-test 931/0 + make run GUI/shell/xHCI 冒烟零 panic（(本次)） |
| 4 | SMAP：CR4.SMAP（CPUID.07H:EBX[20]）+ syscall/ISR 全入口 stac(clac)（条件同 swapgs）+ DEBT-019 重审（SMAP 双层保护） | ✅ | run-kernel-test 931/0 + make run GUI/shell/xHCI 冒烟零 panic（(本次)） |
| 5 | M1 收尾：机制验证 test_f9（EFER.NXE 必断言;SMEP/SMAP CPUID-gated）+ ROADMAP M1✅ + notes | ✅ | run-kernel-test 932/0（发现 SMEP/SMAP WSL2 KVM 不透传 CPUID.07H,代码 CPUID-gated 正确）（(本次)） |
| 6 | M4 Canary：CMake -fstack-protector-strong + boot.S TSC canary + __stack_chk_fail→kpanic | ✅ | run-kernel-test 932/0（(本次)） |
| 7 | M2 KRandom：rdrand/TSC/PIT 熵源 + xoshiro256** + splitmix64 seed | ✅ | run-kernel-test 932/0（test 留批8 ASLR 间接）（(本次)） |
| 8 | M2 ASLR（内核侧）：栈/mmap/brk 地址随机化（ELF base 拆后续：non-PIE 绝对寻址铁证）| ✅ | run-kernel-test 936/0(+4 test_aslr) + make run 冒烟零 panic（`26e8bfa`） |
| 9 | M3 凭证：Task uid/euid/gid/egid + fork memcpy 继承 + getuid/setuid 6 syscall（execve setuid binary 留 F6）| ✅ | run-kernel-test 942/0(+6 test_creds) + make run 冒烟零 panic（`75ad715`） |

### 风险重点
- 批1 sigreturn 改造（碰信号投递核心路径，行为须不变）+ 批2 NXE 开闸（execve 首次真用 NX，可能暴露隐藏配置错）+ 批4 SMAP（碰 syscall entry + 全用户指针访问面）。
- 批2 加 `make run` 冒烟（真用户程序跑通）；批4 加 DEBT-019 重审。

## ✅ F5-M6（e1000 NIC 驱动）— 收官 2026-06-26（PR#40 fb25c89，945/0；批c 中断/netdev 延后交 F7）

> worktree `worktree-f5-m6-e1000`（从干净 main `8be32d4` 拉，零 F9 污染；F9 在 feat/f9-security 上继续，两者零依赖）。F7 网络协议栈被 F5-NIC 阻塞（ROADMAP「F5 网卡→阻塞整个网络栈」），F5-M6 e1000 是 F7-M1 以太网层的地基。整条 HW 跑道（PCI / DmaPool / MSI-X / ISR / poll）已被 xHCI/AHCI 验过，e1000 是现有套路延伸。**polling 优先**（QEMU nested-KVM 不可靠锁存中断）。用户决策「先把驱动打通」，跳过立项 docs。
> 验证：`timeout 40 cmake --build build --target run-kernel-test -j$(nproc)`（带 `-device e1000`）；改 pci.hpp 公共头补全量。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| a | PCI find_e1000 + E1000Controller（BAR0 映射 + 复位 + EERD 读 EEPROM MAC + 链路）+ CINUX_NET gate + test + QEMU -device e1000 | ✅ | 4b4184c | 932/0 + MAC=52:54:00:12:34:56 link=1 + 全量绿 |
| b | RX/TX 描述符环 + 轮询收发 + `net::init()` 生产 boot 接入 + 单播(ARP)/广播(DHCP)收包 + `make run` GUI 冒烟 | ✅ | 95fd10d | 934/0(rebase 前基线 8be32d4;rebase 到 F9 后 target 路径 FAIL,见 b-fix) + GUI 零 panic |
| b-fix | RX 时序:LAPIC timer(0x30)唤醒 sti+hlt 替 trap 循环 hack(test kernel 关中断→main loop 不投递 reply) | ✅ | b4a846b | run-kernel-test **3× 945/0**(ARP+DHCP 真 reply)+ 全量 + test_host 绿 |
| c | TX 完整化 + 中断（MSI / legacy INTx，非 MSI-X）替代 polling + netdev 抽象交接 F7 | ⏳（延后） | | |

> 批a [note](../notes/2026-06-25-f5-m6-e1000-b1-detect-mac.md)；批b [note](../notes/2026-06-26-f5-m6-e1000-b2-rx-tx.md)；**批b-fix [note](../notes/2026-06-26-f5-m6-e1000-rx-timer-fix.md)**。**关键 GOTCHA（接手必读；批b-fix 推翻批b 原结论 ①）**：① ~~轮询 RX 读 MMIO(RDH)才收得到包~~ —— **错的**：批b 那个「读 RDH → GPRC 0→1」是**带 filter-dump 调试时的副作用**，去掉 filter-dump（target 路径）GPRC 稳定 0。真相：**test kernel 关中断 → QEMU main loop 不跑 → SLIRP reply 不投递**。正解 = LAPIC periodic timer(0x30) + poll 没 包时 `sti;hlt;cli`（hlt 让 main loop 投递，timer IRQ 唤醒），**别写 MMIO trap 循环「泵」main loop**（64-trap / filter-dump 都是碰运气）；② QEMU e1000 **不模拟** RCTL.LBM loopback，用 SLIRP ARP/DHCP round-trip；③ 直接跑 QEMU 前要 `regenerate-ext2-image`（否则 ext2 inode 耗尽假失败）；④ `/dev/kvm` 是 root:kvm，手动跑诊断用 `-accel tcg`；⑤ `net_timer_handler` 直接 `g_lapic.eoi()`，**不能** `irq_eoi(0)`（test 没 switch_to_apic，会走 8259 EOI 不了 LAPIC）。

## 🔄 F7 网络协议栈（底子优先：loopback 先 → host 单测 → e1000 收尾）— 2026-06-26 立项

> worktree `worktree-f7-net-ping`（从 main `fb25c89`）。e1000 RX/TX（PR#40）已合，netdev 抽象（F5-M6 批c）延后到此。**用户两度拍板**：① 抽象要做对（加网卡不疼）→ 含零拷贝借用模型 + loopback；② **不急 ping，底子优先**——e1000「梭哈」教训（批b「读 RDH 收得到包」是 filter-dump 副作用，绿了 ≠ 对了）。打法：loopback 当确定性试验台（躲 SLIRP 时序），每层 host 单测站住，e1000 最后接（ping 是栈正确的自然结果，不是 gate）。详见 [L0 note](../notes/2026-06-26-f7-net-l0-netdev-abstraction.md)。
> 验证：每层 host 单测（`test_host`）+ `run-kernel-test`；解耦 4 grep（`kernel/net/` 不 include e1000/dma_buffer/irq）。

| 层 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| L0 | netdev 抽象（NetDevice/NetStack/ProtocolHandler）+ Packet 借用 buffer（BufferSink/scope_guard）+ ARP cache + 复用 Cinux-Base（Span/ScopeGuard/internet_checksum） | ✅ | 8ab2569 | host 4/4（dispatch/buffer/arp_cache/checksum）+ 945/0 + 解耦 grep 成立 |
| L1 | ArpModule + Ipv4Module + IcmpModule + LoopbackDevice → 内核测 ping `127.0.0.1`（确定性，无 SLIRP） | ✅ | 794147f | host net 5/5（+net_arp_module）+ 946/0（+1 test_net loopback ping）|
| L2 | E1000NetDevice adapter（copy RX）+ 生产 arm TX + main 接线 → 真 ping `10.0.2.2`（失败锁死 adapter） | ✅ | 92b82fd | 947/0（+1 test_ping_e1000 真 ping 10.0.2.2 reply）+ host net 5/5 |
| L3 | notes + ROADMAP F7-M1/M2/M3✅ + 4 解耦 grep 落 `check_net_decoupling` target（负测能抓人） | ✅ | 本次 | check_net_decoupling 绿 + 全量绿 |

> **L2 铁证**：`[net] e1000 ping 10.0.2.2: reply id=0x1234 seq=1` —— ARP resolve + ICMP echo 真往返 SLIRP。**底子优先回报**：栈在 loopback 上证明后,e1000 接入一次过。详见 [L2 note](../notes/2026-06-26-f7-net-l2-e1000-ping.md)。**解耦机器执行**：`cmake --build build --target check_net_decoupling`（4 grep,负测往 kernel/net/ 注 irq.hpp 立刻 FAIL）。**F7-M1/M2/M3 收官**（以太网帧/ARP/IPv4+ICMP）。

### ✅ shell ping（生产 net 栈 + SYS_ping + shell 命令）— 2026-06-26，本分支续做
> 把 ping 从"内核测证明"接到 shell。**关键决策：不要常驻 net 线程**——sys_ping 的 send+sti/hlt+poll 循环本身就是 ping 期间的 poll driver（production 开中断，LAPIC tick 唤醒 hlt 驱动 SLIRP）。详见 [shell-ping note](../notes/2026-06-26-f7-net-shell-ping.md)。
>
> ⛔ **ring3 实跑 #DF（未修，接手中）**：B1-B3 代码齐 + 内核态全证（test_production_ping/syscall_ping 绿），但 shell 敲 `ping` → sys_ping 里 sti/hlt → 调度器 syscall 中途抢占 → 破坏栈帧 → Double Fault。harness 抓不到（不能真跑 ring3），boot smoke 才暴露。**协议栈 + ping() 本身没问题**。根因 + 修法（阻塞式 ping + 常驻 poll driver，**不要 sti/hlt 自旋**）见 [handoff-df note](../notes/2026-06-26-f7-net-handoff-df.md)。**B1-B3 commit 暂不要 push 进主线**（敲 ping 会 panic）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| B1 | 生产 `cinux::net::init()`(建栈+attach e1000 单例,声明 kernel/net/ 实现 drivers/net/)+ `ping()`(sti/hlt 循环)+ stub + main 接线 | ✅ | c0c8ddd | test_production_ping reply id=0xbeef + 948/0 |
| B2 | `SYS_ping=220` + sys_ping.cpp(调 cinux::net::ping,IP 解包+errno)+ 注册 syscall_table | ✅ | b0c817f | 948/0(syscall 注册编译) |
| B3 | sys_ping 用户桩 + cmd_ping.cpp(解析 IP+count,循环打 reply)+ shell 注册 + make run 冒烟 | ✅ | 98990a4 | test_syscall_ping rc=0 + 949/0 + make run `[net] L3 stack up` 无 panic |

> **live 交互**：`make run` → Shell → `ping 10.0.2.2`（输出走 GUI 屏,串口捕不到；路径全证：sys_ping handler→reply 内核测 + cmd_ping 编链 + 生产 boot 起栈 + syscall 派发同现有 syscall）。残留：常驻 poll driver(被动收包)/socket 层(F7-M6)/中断(批c)/UDP·TCP。

> **架构**：两轴分离（NetDevice 设备轴 / ProtocolHandler 协议轴）+ FOLD-A（mac 可选 / 设备自决 L2 帧）+ FOLD-B（设备表 kMaxDevs=2 / `on_frame` 透传 `NetDevice&`，栈无 singleton）。e1000 copy adapter **不碰 E1000Controller**；loopback 零拷贝 RX 证明借用通路；virtio 零拷贝 future（`supports_zerocopy()` 诚实声明）。buffer 三红队：UAF = copy-to-retain 契约 / drop = scope_guard 全出口 recycle / 重入 = loopback send 只入队下轮派发。

### 🔄 F7-M4 UDP（协议层 + 端口多路复用）— 2026-06-30 立项

> worktree `worktree-f7-m4-udp`（从干净 main `1cdd507`，三路并行之一，只碰 `kernel/net/`）。接 M1/M2/M3：在 IPv4 同层加 UDP。**用户拍板两决策**：① IPv4 接 UDP 用 proto 表（加 `L4Handler` 缝 + `Ipv4Module` 内部 proto→handler 表，**ICMP 自动迁入表**还掉 [ipv4.cpp:75](../../kernel/net/ipv4.cpp) 的 TODO，单一 L4 分派机制）；② e1000 UDP 做 TX-only smoke（SLIRP 无 UDP echo，不期望 reply）。底子优先：host 单测 → loopback 确定性 round-trip → e1000 TX 收尾。范围栅栏：socket API(sys_socket/bind/recvfrom) 留 F7-M6、TCP 留 F7-M5、ring3 ping #DF 是另一条线不碰、不进生产 `net::init()`（无消费者）。详见 [批1 note](../notes/2026-06-30-f7-m4-b1-udp-protocol-layer.md)。
> 验证：每批 `timeout 120 cmake --build build --target run-kernel-test-all -j$(nproc)` 两 leg 绿（基线 967/0；**本地必须** `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_BUILD_TESTS=ON`，否则 smoke 挂死 + test target 缺）；改公共头（ipv4.hpp）push 前补全量 `cmake --build build`。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 1 | UDP 协议核心：`udp.{hpp,cpp}`（UdpHeader parse/build + UdpModule send/handle/bind/unbind + 伪首部校验和连续缓冲区法）+ `L4Handler` 缝进 ipv4.hpp + `Ipv4Module` proto 表（ICMP 迁入 ctor 注册，删 TODO）+ `kIpProtoUdp=17` + host 单测 `test_net_udp`（9 例：头/线序/round-trip/校验和损坏/checksum=0/无 listener/双端口/unbind/重复 bind） | ✅ | 本次 | ctest 63/63（+net_udp）+ run-kernel-test-all 两 leg 967/0（ICMP 走表实证 loopback+e1000+production+sys_ping ping reply 全通） |
| 2 | loopback UDP round-trip 内核测：`test_net.cpp` 加 `test_udp_loopback`（127.0.0.1 UDP send→单次 poll→listener 收到 payload+端口，确定性躲 SLIRP） | ✅ | 本次 | run-kernel-test-all 两 leg 968/0（+1 UDP）+ `[net] loopback UDP: 6 bytes` 两 leg实证 |
| 3 | e1000 UDP TX smoke（发 UDP 到 10.0.2.2，断言 ARP resolve + send ok，不期望 reply）+ ROADMAP F7-M4✅ + todo 03-udp 打勾 + note + PLAN 收官 | ✅ | 本次 | run-kernel-test-all 两 leg 969/0（+1 UDP TX）+ `[net] e1000 UDP TX -> 10.0.2.2: ARP resolved + send ok` 两 leg实证；check_net_decoupling 绿 |

> **F7-M4 收官**（2026-06-30,feat/f7-m4-udp 3 commit 待 PR）:UDP 协议层(封装/解析/端口多路复用) + Ipv4Module L4 proto 表(ICMP 迁入,还掉 TODO)。三批:协议核心+host 单测(63/63) → loopback 内核 round-trip(968/0) → e1000 TX smoke(969/0)。**架构**:L4 分派单一机制(proto→handler 表),加 TCP 不疼。范围栅栏:socket API/M6、TCP/M5、不进生产 net::init(无消费者)。GOTCHA(过程):会话续接 cwd 重置回主仓,build 跑错仓库一度误判——ELF grep UDP 字符串 + pwd 排破,重进 worktree 重验。详见各批 note。**push/PR 归用户**。

> **GOTCHA**：① ctor 里 `add_l4(kIpProtoIcmp, icmp)` 需 `IcmpModule&→L4Handler&` 转换，但 ipv4.hpp 只前向声明 IcmpModule（include icmp.hpp 会循环）→ **ctor 定义挪到 ipv4.cpp**（.cpp include icmp.hpp 完整类型）；② UDP 校验和用连续缓冲区 [伪首部 12 | UDP 头 8 | payload] 一把 `internet_checksum`，避开 partial+手搓拼接；③ `h.length`（非 delivered 字节数）重建伪首部 + 求和匹配 sender；checksum=0 跳过验证，TX 算出 0 发 0xFFFF。

## ✅ F-GUI-DECOUPLE（GUI 模块独立化 / 消源码 #ifdef）— 收官 2026-06-26（PR#35 8be32d4；促修 SMP 迁移竞态 PR#34）

> 横切里程碑（接 F-CLN）。目标：消 main/init/irq 的源码 `#ifdef CINUX_GUI/CINUX_USB` 读半截路（§14 真违规），让开关全归 CMake。CMake 文件级 gate 框架已就位（F-CLN 清了 keyboard/pit），只剩抽象 + stub 化的机械活。来源：2026-06-25 用户「考虑 GUI 分离」+ memory `gui-decouple-milestone`（独立里程碑，不混 #ifdef 清理批）。
> 范围（用户拍板）：核心 4 批（main+init+irq）；test 文件 `#ifdef` 整体守卫（§14 灰色边缘）留 follow-up。
> 验证：每批 `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` 绿 + `make run` GUI 冒烟；批1/4 加非 USB / 非 GUI 构建验证。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 1 | USB 开关归 CMake（usb_stub 空壳 + usb::poll_input 接口 + CMake else）→ init.cpp 消 3 处 `#ifdef CINUX_USB` | ✅ | 7b2cdcc | 931/0 + GUI 冒烟零 panic + 非 USB big_kernel 构建绿（usb_stub 链接） |
| 2 | `launch_userspace()` 抽象 + GUI/non-GUI 两实现 + CMake gate → 消 init.cpp 头号反例（§14） | ✅ | 7e32888 | 931/0 + make run -smp 2 panic 0（触发并促修 SMP 迁移竞态 PR#34） |
| 3 | `handoff_framebuffer_to_gui()` + main.cpp Step15b 一句化 → 消 main.cpp `#ifdef CINUX_GUI` | ✅ | f39cbe8 | big_kernel 编译 + 931/0 + -smp 2 panic 0 + 非 GUI 非 USB big_kernel 绿 |
| 4 | irq_handlers stub 文件化（mouse_stub + usb_xhci_stub）→ 消 `#ifndef` | ✅ | eaaca9c | 931/0 + -smp 2 panic 0 + 非 GUI 非 USB big_kernel 绿 |
| 5 | USB 拆核心传输(`if USB`)/HID(`if USB AND GUI`)双 gate + usb_stub/usb_xhci_stub → 非 GUI+USB big_kernel 链接 | ✅ | d4012c6 | 默认 931/0 + -smp 2 panic 0 + **非 GUI+USB big_kernel 绿（原断链现修）** + 非 GUI 非 USB 绿 |

> **预存 follow-up**：非 USB `big_kernel_test` 因 `test_xhci.cpp` 无 `#ifdef CINUX_USB` 守卫链接失败（test gate，用户拍板 test 留）。~~非 GUI+USB big_kernel 断链~~ ✅ 批5 修。详见 [note](../notes/2026-06-25-f-gui-decouple-b2-5.md)。

## ✅ F4-followup（SMP 任务迁移竞态修复）— 2026-06-25，已合 main PR#34（原 feat/smp-migration-fix）

> 来源：F-GUI-DECOUPLE 批2（launch_userspace 抽函数）触发 `make run -smp 2` 必现 panic，诊断为预存 SMP bug（批1 内联时序避开，批2 必现；根因 F4-M4 后潜伏，对应"偶现崩溃"痛点）。
> **根因**：任务跨 CPU 迁移时，旧 CPU `context_switch`（存 task->ctx）与新 CPU（取同一 ctx）并发 → ctx 写花 → ret 跳垃圾 rip → #UD/#BP。F4-M4「pick removes」挡双 CPU 同运行，但漏迁移窗口。
> **修复**（对齐 Linux `task_struct->on_cpu`）：Task `on_cpu` + context_switch.S 存完设 -1 + pick_next 跳过（别的 CPU 正存，本 cpu 不跳保单核 yield）+ schedule/exit_current/run_first claim + build/run_first/setup_ap_idle 初始化。**用 pick 跳过而非 spin**（互 spin 死锁）。
> **验证**：单核 931/0 + `make run -smp 2` 连续 3 次 0 panic。详见 [note](../notes/2026-06-25-f4-followup-smp-migration-race.md)。
> **状态**：已合 main PR#34（commit `68b1913` + docs `cd3b2b9`）。F-GUI-DECOUPLE 批2 已可 resume（-smp 2 不再 panic）。

## ✅ F-CLN 债务清理（横切，接 F-QA）— 收官 2026-06-25

> **目标**：开 F10 libc / F7 网络大弧前，把 [debt.md](../todo/quality/debt.md) 的 open 债收敛 + 补全 xHCI/USB 审计盲区（Q3 后合入未审），让新弧在干净地基上推进。
> **来源**：2026-06-25 自检（DEBT 全表核对 + OPEN GOTCHAS + xHCI 未审面 + 报告）+ 用户决策「专门开清理事务，全部批在 `feat/f-cln-debt` 一个分支搞定」。
> **命名**：F-CLN（债务清理，与 F-QA「质量收敛」职责区隔）。
> **范围**：批0-8（必做批0-3 + 中危批4-7 + 收尾批8）。DEBT-019/013/020/012（用户指针 + ELF）**留 F10 顺手修**（与 F10 syscall/execve 强相关）；DEBT-011/014（低危）收尾时看。
> **验证**：每批 `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` 绿才提交；改公共接口/mock 补全量；批4/7 加 `-smp 2`；收尾加 LOCKDEP + host-ASAN。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 0 | xHCI/USB 专项审（D2 内存+D3 并发+D4 生命周期，deterministic 四段式，只读→报告+新债登记） | ✅ | (本次) | docs-only；D2/D4 清洁，D3→**DEBT-021(P1)** |
| 1 | DEBT-015 核实 PathBuf 改堆后 frame-larger-than 残余 + 启用门禁/关债 | ✅ | (本次) | 931/0;sys_dmesg LogEntry[16]→堆,big_kernel 零 frame 命中,门禁 warning 级(GCC 限制 -Werror= 不可行) |
| 2 | DEBT-016 test framework 加 ASSERT_OK 宏 + 清 32 处忽略 + 去 -Wno-unused-result | ✅ | (本次) | run-kernel-test 931/0 + host ctest 54/0;零 ignoring |
| 3 | DEBT-018 kMaxCpus 统一单一权威（percpu=8 vs acpi=16）+ static_assert | ✅ | (本次) | 931/0 + -smp2;acpi 改名 kMaxAcpiLapics + static_assert |
| 4 | DEBT-008 signal_setup_frame 写帧前校验栈 VMA（中风险，信号路径） | ✅ | (本次) | 931/0 + make run 冒烟;校验 fallback signal_exec_default |
| 5 | DEBT-009 clear_user_mappings + free_subtree 识 huge entry | ✅ | (本次) | 931/0;三处 huge 检测(防御,warn+不下钻) |
| 6 | DEBT-010 FDTable refcount guard()→irq_guard()/atomic 对齐 R3 | ✅ | (本次) | 931/0 + host ctest + -smp2;acquire/release atomic |
| 7 | DEBT-007 quantum_remaining_ 改 per-task（中风险，调度核心 + -smp2 回归） | ✅ | (本次) | 931/0 + -smp2 + ctest54/0;Task::quantum_remaining per-task |
| 8 | 收尾：ROADMAP/PLAN/debt/notes + 全量 + -smp2+LOCKDEP+host-ASAN 验证矩阵 | ✅ | (本次) | 931/0 + -smp2 + LOCKDEP 931/0 + ctest54/0 |

> **关键发现（立项核实）**：DEBT-015 核心已修——`sys_creat.cpp` 等已用 `PathBuf`（堆，path.cpp:19 注释 "was char[PATH_MAX] on the stack"），`grep '[PATH_MAX]' kernel/syscall/` 空。debt.md 滞后，批1 降级为"核实残余 + 关债"。DEBT-007 仍 open（quantum 仍单一共享 RoundRobin 成员，F3-M4 仅从 Scheduler 移入类，未 per-task/per-CPU）。
> **风险重点**：批4（信号投递路径，GOTCHA#16/#11 相关，真程序高频锻炼）、批7（调度核心 tick/pick_next，-smp2 回归必做，GOTCHA#23 同族）—— 各自 `-smp 2` 验证。
> **架构契合**：A.6 ErrorOr（批2 ASSERT_OK 强化铁律）；A 单一定义（批3 kMaxCpus 消 ODR）；A 禁 RTTI/禁异常（全批遵守）；对齐 Linux（批7 per-task quantum / 批4 expand_stack / 批5 huge free）。
>
> **收官（2026-06-25）**：批0-8 全 ✅。修 7 债（DEBT-015/016/018/008/009/010/007）+ xHCI 审登记 1 新债（DEBT-021，poll_events 并发，留 xHCI 重构）。验证矩阵全绿：run-kernel-test **931/0** + **-smp 2** ALL PASSED + host ctest **54/0** + **LOCKDEP 931/0 零误报**。残留 open：DEBT-019/013/020/012（用户指针+ELF，留 F10 顺手）+ DEBT-011/014（低危）+ DEBT-021（xHCI 并发，留 xHCI 重构）。F-CLN 在 feat/f-cln-debt 分支（10 commit 待 PR）。详见各批 notes（`document/notes/2026-06-25-f-cln-b{0..8}-*.md`）。

## ✅ F13-A Cinux-GUI 主体(独立仓 P0-P7)— 跑通(2026-07-05;CinuxOS 子模块 pin `4c84eb2` 待 bump 到 `7a159c4`)

> GUI 抽成独立跨平台库 **Cinux-GUI**(原 visor 名已废,留给用户嵌入式线;org 仓 `Awesome-Embedded-Learning-Studio/Cinux-GUI` 作 `third_party/Cinux-GUI` submodule)。同一份 core 跑 Cinux 桌面 / Linux(fbdev+evdev)/ SDL / 未来 MCU/GPU。核心机制 = **Host ABI 函数指针表([core/host.hpp](../../third_party/Cinux-GUI/core/host.hpp))唯一硬边界**,换宿主只换表填充。
>
> **Cinux-GUI 仓主体 P0-P7 全跑通**(独立 checkout `~/Cinux-GUI`,main=`7a159c4`,**领先 CinuxOS 子模块 pin 66 commit**):P0 staging 归 core+swraster+offscreen/replay host → P1 Linux fbdev+evdev host(QEMU 冒烟 PASS)→ P2 Scene+Compositor+帧间 dirty diff → P3 Widget 树(PaintList+Material Theme+控件)→ P4 Window/WindowManager/TerminalWidget+真 shell PTY → P5 字体缩放/flex/per-widget dirty/ANSI 彩色 → P6 TextBox/resize/Scene 退役(Compositor handler OCP)/256 色 → P7 Radio/Dropdown+sdl-host+gallery demo。
>
> **Host ABI 表 P0-P7 零改动**(HostCore 8 回调 + HostDesktop.spawn + Host{core,desktop*,ctx} 全稳定)。变的只是 core 内部:staging 所有权 host→core(`GuiCore` 类,`pump(Host*)`→`GuiCore::pump()` 成员、`Frame.pixels` const→void)、桌面层 Window/WindowManager/TerminalWidget 从 host 搬进 core `widget/` 子树、Scene 在 P6-d 退役 → `Compositor` 类 + `Desktop::render`。
>
> **后续主体**(Cinux-GUI 仓独立开发,用户主导):P8 GPU texture compositor / P9 多进程 Surface 协议 / MCU 线(STM32F1)。详见 [Cinux-GUI 仓 ROADMAP/PLAN/DIRECTIVES](https://github.com/Awesome-Embedded-Learning-Studio/Cinux-GUI)。
>
> **F13 在 CinuxOS 侧收窄为 host adapter 重写 → 见下面 🔄 F13-B 段。**

---

> **历史进度(2026-06-21→06-23,已合 main,保留作 F13-A 背景):**
>
> ⚠️ **DRAFT**。方向可能调整。来源:11-agent workflow（5 维调研 + 综合 + 3-lens 对抗 + 完整性）+ 用户 4 决策。
>
> **方向转变**:GUI 从「Cinux 内联」抽成独立跨平台库 **visor**——同一份 core 跑 Cinux 桌面（现内核态/未来用户态）、单片机 LCD/OLED（小到 STM32F103）、未来显卡。内核只提供最小能力,不感知 GUI 风格。核心机制 = **Host ABI 函数指针表（visor_host.h）唯一硬边界**,换宿主只换 5 张表填充（=「不感知是否用户态」）。
>
> **用户 4 决策**:① 完整控件工具箱 ② Cinux 桌面先 ③ MCU 全规模含 STM32F1 ④ GPU 可插拔先软件。
> **诚实预期**:STM32F1 与桌面控件库是两个 profile ceiling（LVGL/u8g2 亦然）,visor 短期承诺「比现在好看的 Cinux 桌面 + MCU 仪表盘」,完整 macOS/Windows 级是 L4b 长弧。
>
> **文档**（全 DRAFT）:[todo/f13-gui/README.md](../todo/f13-gui/README.md) + [visor-01-presets.md](../todo/f13-gui/visor-01-presets.md)（profile/宏/约束/ABI 骨架）+ [visor-02-refactor-and-separation.md](../todo/f13-gui/visor-02-refactor-and-separation.md)（重构执行计划）+ notes [research](../notes/2026-06-21-f13-visor-research.md) / [architecture](../notes/2026-06-21-f13-visor-architecture.md) / [roadmap](../notes/2026-06-21-f13-visor-roadmap.md)。
>
> **进度(2026-06-22)**:§1 spawn 公共化 ✅(`82e9023`)+ §2 PIT 反转(worker pump 驱动)✅(`0edc70f`)+ §3a Host ABI 骨架 ✅(`69cc534`,核心表+Desktop extension+定宽事件头+编译期自检)+ **§3b Host ABI adapter 接线 ✅(`ff462c0`)**:visor_pump 经 Host ABI 函数指针表驱动 GUI(drain/dispatch/spawn/composite,内部 NULL-guard 所有 host 回调)+ Cinux adapter 填核心表(poll_event 序列化 cinux::gui::Event↔visor_event / now_ms=PIT uptime / alloc=kmalloc / log=kvprintf / flush 占位留 §4)+ Desktop extension(spawn→create_shell_terminal)+ visor_event_payload.h(POINTER 18B kind 区分 move/down/up / KEYCODE 3B packed)。**visor_pump 取代 gui_pump 成唯一 pump(无开关——visor 目的即分离,删 gui_pump 免双份逻辑)**。887/0 + `make run` 冒烟([visor] init+desktop+worker 无 panic);4-agent 对抗 review 0 bug,修 5 warning+9 nit(keycode 单源 polarity / now_ms fold 进 pump+NULL guard / host NULL check / kind 双射)。详见 `document/notes/2026-06-22-f13-visor-host-adapter.md`。**§4a SwRaster 原语骨架 ✅(`5f2377c`)**:visor L3 软件光栅化原语(fill_rect/blit/blit_blend Q8.8 定点/glyph_blit 1bpp mask/draw_line + ClipRect 半开 clip),从 canvas draw_* 泛化,纯 CPU/整数(VISOR_NO_FPU 安全),不接管 composite,12 项单测(run-kernel-test **899/0**,+12)。详见 `document/notes/2026-06-22-f13-visor-swraseter.md`。**§4b Region 一等 ✅(`24d1dda`)**:half-open `Rect`(intersect/union/subtract 4-strip/translate/contains)+ bounded `Region`(kMaxRects=32,溢出坍缩包围盒——**永不欠覆盖**,脏区欠覆盖即丢像素)+ 21 单测(run-kernel-test **920/0**,+21)。纯整数定长无分配无 host 耦合,为 §4c 脏区铺路。详见 `document/notes/2026-06-22-f13-visor-region.md`。**§4c dirty+flush 接管 ✅(`a3bd518`)**:display path 经 Host ABI——`composite()` 仅渲 staging back buffer(退役 `Canvas::flip`),pump 经 `host->core.flush` 逐 rect 转发脏区,Cinux `cinux_flush` 真实转发(staging base→VBE framebuffer,stride/pitch)。脏区用 §4b Region:**光标移动**(旧+新 footprint)精确 flush / **结构变化+terminal 输出**全屏 / **空闲帧零 framebuffer 写**(idle-skip)。`poll_output`→bool + `content_dirty_`(无管道 on_key)。**4-lens 对抗 review(20 agent,6 confirmed)全修**:flush w/h 钳到 fb 维度(防溢出)/ terminal content-dirty(防丢帧)/ +7 脏·flush 集成测(首帧全屏/idle-skip/invalidate→flush/dirty 清)。run-kernel-test 920→**927/0** + `make run` 冒烟桌面无 panic。详见 `document/notes/2026-06-22-f13-visor-dirty-flush.md`。**§4d colorkey→alpha ✅(`9acef71`)**:废弃 `draw_bitmap` 的 `0x00000000` colorkey,改真 alpha——icon 编译期 1-bpp mask(从 `build_icon` 同一份 row-strings 生成,`mask bit = nibble≠0`,独立于颜色值,与 visor glyph mask 同 MSB-first 约定)。`Canvas::draw_bitmap_masked`(set bit 才绘;null 全不透明)+ `DesktopIcon.mask` + `draw_desktop_icons` 走 mask。**行为不变**(唯一透明项 palette[0]=BLACK → color≠0 ⇔ nibble≠0 ⇔ mask set 三者等价,逐像素一致);价值=纯黑可绘 + 透明不由颜色值定。+3 测(set bit/null mask 全绘含纯黑/越界裁剪)。run-kernel-test 927→**930/0** + 冒烟无 panic + 全量编译绿。详见 `document/notes/2026-06-22-f13-visor-colorkey-alpha.md`。**F13 §4 全域收官(§4a-d)**:SwRaster 原语 → Region 一等 → dirty+flush 接管 → colorkey→alpha。887→930/0(+43)。**pump 解耦为 host-neutral ✅(`8a4add4`,分离前 Step 1)**:`visor_pump.cpp` 零 cinux include——瘦成 drain `poll_event`→`dispatch_event`→`render_frame`→逐 rect `flush`;Host ABI +`dispatch_event`/`render_frame`/`visor_frame` POD;cinux GUI 逻辑全下沉 adapter(`cinux_dispatch_event`+`cinux_render_frame`)。core(ABI+region+swraseter+pump)可独立编译 → 下步物理 submodule 化纯机械。顺带全 visor 头 `#pragma once` + `typedef`→命名类型(`struct X`/`enum X`,C 兼容)。run-kernel-test 930→**928/0**(pump 测 policy 移 adapter)+ 冒烟无 panic + 全量+test_host 绿。详见 `document/notes/2026-06-22-f13-visor-pump-decouple.md`。**Step 2 物理分离到顶层 visor/ + 独立构建 ✅(`a77780e`)**:`git mv` 11 核心文件→`visor/core/`,adapter(2 文件)→`kernel/gui/`(`visor_core/` 目录清空);核心内部同目录 bare include 零改动,adapter+消费者(window_manager.hpp / init.cpp / gui_init / 3 个 test_visor_*)用仓库根相对路径改 `visor/core/*`。`visor/CMakeLists.txt` 单文件双构建(`CMAKE_SOURCE_DIR` 守卫):作 kernel 子目录只建 `visor_core` STATIC lib / 作 build 根(`cmake -S visor`)加 `project` + harness + ctest + `-Wall -Wextra`;`big_kernel`+`big_kernel_test` 各链 `visor_core`。`visor/host/fake_host_main.cpp` 零-kernel 中立性证明(手填 host 表驱动 visor_pump:null-host 安全 / idle-skip / dirty-flush / region 代数)= SDL/MCU host adapter 种子。**独立构建 hosted 编译器零 kernel include + ctest 1/1 + `-Wall -Wextra -Werror` 零警告**;run-kernel-test **928/0** + GUI 冒烟零 panic(adapter 加载 + desktop 渲染)+ test_host 绿。`visor/` 子树可 verbatim 拷去 SDL/MCU 项目。详见 `document/notes/2026-06-22-f13-visor-visor-separation.md`。**改名 visor→cinux::gui / cgui_ ✅(`0baa6d0`)**:PC GUI 框架迁独立仓库 `Cinux-GUI`,`visor` 名留给用户嵌入式线,全树改名切割。C++ ns `visor::`→`cinux::gui::`(与内核同 ns 并存,审计零撞车)、主入口 `cinux::gui::pump`;C ABI 前缀 `cgui_`/`CGUI_`;文件/目录 `visor_*`→`cgui_*`、`visor/`→`cgui/`;adapter `cgui_host_cinux.*`、`cinux_host(_init)`。机械 6 步 sed+perl(pump 函数 vs 文件名 perl 负向前瞻分离),GOTCHA:ns 声明 `namespace visor` 被 `\bvisor\b` 误中为 `namespace cgui`,手工修回 `namespace cinux::gui`(4 文件)。详见 `document/notes/2026-06-22-f13-cgui-rename.md`。验证:独立 cgui 构建+ctest 绿 / run-kernel-test **928/0** / GUI 冒烟 `[cgui]` adapter 初始化零 panic / test_host 49/49 / 全树零 visor 遗留。分支 `feat/f13-visor` 领先 origin(refactor `a77780e` + docs `9a17833` + rename `0baa6d0`),待用户 push 更新 PR #29。**Cinux-GUI 独立仓库迁移收官(2026-06-23,4 相)**:① Phase A 改名(`0baa6d0`,见上);② Phase B subtree split lift 到 org 私有仓 `Awesome-Embedded-Learning-Studio/Cinux-GUI`(用户 push;GOTCHA:seed 作者邮箱误编 `charliechen@users.noreply.github.com`→认成另一 CharlieChen 账号,`--amend --reset-author` 回真身 `725610365@qq.com` + force-push);③ **Phase B2 去 cgui_ 前缀 + C ABI 进 `cinux::gui` 命名空间(submodule `c15bd9d`+`7d3b9aa`)**:文件 `.h`→`.hpp` 去 cgui_ 前缀(顺修 swraseter→swraster),`Host`/`Frame`/`HostCore`/`EventHeader`/`PointerPayload` 进 ns,`PixelFormat` enum class,事件常量 k-constexpr,`cgui_rect` 并入 `Rect`,**去 extern "C"**(MCU 砍无 C 消费者),**MCU 字段删**(`conf.hpp` 整删 + HostCore 去 enter/exit_sleep/next_deadline + PixFmt 只留 XRGB/ARGB),target `cgui_core`→`cinux-gui`。**GOTCHA(撞名)**:核心事件枚举原名 `EventType` 撞内核 `cinux::gui::EventType`(Mouse*)→重定义,改核心为 **`EventCode`**(内核无,不撞);这是选 `cinux::gui` 扁平 ns 的唯一撞点。④ **Phase C submodule 接管(`2d65eba`)**:in-tree `cgui/` 删,`third_party/Cinux-GUI` submodule(pin `7d3b9aa`),adapter `cgui_host_cinux`→`host_cinux` + 类型 `cinux::gui::*` + 删 MCU 字段赋值,消费者 include `third_party/Cinux-GUI/core/*`,test `test_cgui_*`→`test_gui_*`。run-kernel-test **928/0** + GUI 冒烟零 panic + test_host 绿 + 独立构建 ctest 1/1。详见 `document/notes/2026-06-23-f13-visor-cinux-gui-migration.md`。分支领先 origin 5 commit,**待 push(submodule `7d3b9aa` 先→父仓后,pin 才解析)**。**后续**:Cinux-GUI 主体(M0-M9 widget + SDL/X11/Wayland host)用户独立开发;F13 follow-up(dirty lowering / 合成器只重绘脏区 / SMP TLB shootdown)。**v2(2026-06-21)** 吸收外部审查:L0 Host ABI 收缩、Display flush 模型、GPU texture compositor、PIT 反转提前、Region 一等。
## 🔄 F13-B CinuxOS host adapter 重写接新 core — 立项 2026-07-05（worktree `.claude/worktrees/f13-gui`，从干净 main `bef86b0`）

> CinuxOS 侧 host adapter（[kernel/gui/host_cinux.cpp](../../kernel/gui/host_cinux.cpp)）还停在 2026-06-23 Scene 时代：`render_frame` 调老 `wm.composite()`（P6-d 已退役死路径）、[desktop_launch.cpp](../../kernel/gui/desktop_launch.cpp) 调自由函数 `cinux::gui::pump()`（新 core 已删，改 `GuiCore::pump()` 成员）、子模块 pin 落后 66 commit。老桌面层 6 文件（window/window_manager/terminal/...）+ 老 GUI 测试 ~5849 行，新 core 已 1:1 吸收但 CinuxOS 侧还留着（跟 Canvas/Pipes/TaskBuilder/launch_user_program/g_pid_alloc 缠绕）。**这是 [Cinux-GUI DIRECTIVES D 段](https://github.com/Awesome-Embedded-Learning-Studio/Cinux-GUI)明说的「CinuxOS 侧唯一对接点 = host adapter + framebuffer handoff」**。
>
> **用户 2 决策（2026-07-05）**：① **纯渲染优先**（B1a 切新 core，`desktop=NULL` 无终端，B2 单独做 PTY 真终端）；② **这次一起清理**（老桌面层 + 老测试 B1b 删，GUI 净减 ~7300 行）。
>
> **关键利好**：Host ABI 表形态 P0-P7 零改动，host adapter 重写 = 换 `render_frame` 内部调用（`wm.composite()` → `Desktop::render`），不是改 ABI 表；Cinux-GUI 仓 [host/linux_fbdev_main.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Cinux-GUI/blob/main/host/linux_fbdev_main.cpp) 是现成重写模板（fbdev+evdev 同海拔，跟 CinuxOS VBE+输入最近）。

### 批表
| 批 | 范围 | 状态 | 验证 |
|----|------|------|------|
| 0 | 立项 docs（本段）+ ROADMAP F13 行（F13-A/F13-B）+ todo/f13-gui/ superseded | 🔄 | docs-only |
| 1a | bump pin `4c84eb2`→`7a159c4` + host adapter（[host_cinux.cpp](../../kernel/gui/host_cinux.cpp)）全切新 core（`render_frame`→`Desktop::render`、`dispatch_event`→新 `wm.process_pointer/dispatch_key`、pump→`GuiCore::pump()`，持 HostState=Desktop+WindowManager+GuiCore+Theme+PsfFont）+ desktop_launch/handoff 切 + gui_init 构新 widget 树（`desktop=NULL` 纯渲染）+ freestanding 头门禁验；**老桌面层/老测试留对照** | ⏳ | 两 leg run-kernel-test-all（老测试留）+ GUI 冒烟（新 widget 桌面纯渲染，用户自启） |
| 1b | 删老桌面层 6 文件 + desktop_icon/icon ~1500 行 + 老 GUI 测试 ~5849 行 + kernel/gui/CMakeLists + main_test.cpp wiring；test_terminal_shell/multi_terminal 留（B2 改写测新 TerminalSession）；Canvas 看是否退役 | ⏳ | 两 leg run-kernel-test-all（测试数大降）+ 全量编 + GUI 冒烟回归 |
| 2 | PTY 真终端：kernel-side `TerminalSession`（PTY master inode+shell_pid+新 TerminalWidget*）+ `HostDesktop.spawn` 真实现（fork+execve /bin/sh + 挂 PTY slave，复用 F10-M3 PTY 框架）+ render_frame 每帧 drain PTY→`TerminalWidget::write` + 键盘路由 PTY stdin | ⏳ | GUI 真跑 shell（用户自启验） |
| 3 | 收官：note + ROADMAP F13-B ✅ | ⏳ | docs-only |

### 风险 / 陷阱
- **硬前置捆绑**：bump pin + `render_frame` 切 `Desktop::render` + pump 切 `GuiCore::pump` 三者必须同批（bump 后 `pump.hpp` 没了立刻编不过）。B1a 一口气做。
- **freestanding 头门禁**：新 core `widget/` 子树可能引禁止头（`<memory>` 等，[[kernel-can-use-std-smart-ptr]]），bump 后全量编验证，必要时 per-file EXEMPT 或 host adapter 隔离。
- **新老 WindowManager 类名冲突**：老 `kernel/gui/window_manager.hpp` vs 新 `third_party/Cinux-GUI/core/widget/window_manager.hpp` 可能同名；host_cinux/gui_init 只 include 新 core（不 include 老头）避撞，B1a 先验。
- **GUI 验证用户自启**（[[gui-verification-user-starts-always]]）：Claude 只准备环境（GUI=ON + rebuild + assemble rootfs），不 background 启 GUI QEMU；console gate（run-kernel-test-all）Claude 跑。
- **bump pin 顺序**：submodule pin 改父仓 worktree commit（用户控制 push）；Cinux-GUI 仓 main 已是 `7a159c4`（无需 push 子模块）。
- **`desktop=NULL` 中间态无终端**：B1a/B1b 后 GUI 桌面纯渲染（图标+控件），无终端；B2 补真终端。中间态可接受（用户决策 ①）。

## ✅ F5-M5 xHCI USB 主控驱动（MSI-X）— 全域收官，已合 main（PR#30 + PR#31 `9f70301`）

> **目标**：xHCI 主控 → USB HID boot 鼠标+键盘 → 现有 `Mouse::event_queue()`；**正经做 MSI-X**（本里程碑内建,非 INTx 捷径）;PS/2 作 fallback 保留（USB primary 时禁其喂队列,保 SPSC）。计划见 `~/.claude/plans/synthetic-meandering-cascade.md`。
>
> **地基（8-agent recon 实证）**：DMA 池/PCI/APIC/VMM/事件队列可复用;补 PCI `find_xhci` + cap 遍历 + MSI-X + 新 IDT 向量。MMIO 分槽:xHCI BAR0@+0x20000、MSI-X Table@+0x21000、PBA@+0x22000。
>
> | 批 | 范围 | Commit | 测试 |
> |----|------|--------|------|
> | 0A | MSI-X 能力发现（纯 reader 注入）+ host 单测 | 2714846 | run-kernel-test 928/0 + test_msix 11 例 |
> | 0B | MSI-X Table/PBA MMIO 映射 + 条目编程 + 使能 | a1be57e | run-kernel-test 928/0 + test_msix 17 例 |
> | 0C | 向量安装 helper + ISR/handler 注册 | e497e4b | run-kernel-test 928/0(向量注册,触发留 2C) |
> | 1A | PCI find_xhci | ab37fba | run-kernel-test 928/0 + test_pci 7 例 |
> | 1B | xHCI 寄存器布局头 | f7278b0 | run-kernel-test 928/0 + test_xhci 6 例 |
> | 1C | 控制器 init（使能/MMIO/handoff/reset） | 6f3e24e | run-kernel-test-xhci **929/0**（真 xHCI reset 点亮!) |
> | 2A | TRB + ring 数学（纯） | 558a781 | run-kernel-test 929/0 + test_xhci 11 例(含 5 ring) |
> | 2B | DCBAA + 中断器/ERST + 启动 | 265b9c1 | run-kernel-test-xhci **929/0**(控制器 running,scratchpad=15) |
> | 2C | 接线 MSI-X→event-ring ISR + doorbell NOOP→中断（最高风险） | 1b89843 | run-kernel-test-xhci **929/0**(cmd_completions=1 EINT=1,命令管线+中断路径端到通) |
> | 3A | USB SETUP 包+描述符+xHCI context 编码器(纯层,host 测) | 9bfd7a4 | run-kernel-test **929/0** + test_host 53/53(context 位布局已 Linux xhci.h 核实) |
> | 3B | Address Device 状态机(端口 reset+Enable Slot+寻址) | c51065b | run-kernel-test-xhci **930/0**(address device code=1 slot_state=2 dev_addr=1,真机设备 addressed) |
> | 3C | 控制传输(SETUP/Data/Status)+ GET_DESCRIPTOR + SET_CONFIGURATION | 48b5ff4 | run-kernel-test-xhci **930/0**(读到 usb-kbd 描述符 vid=0x627 + set_configuration ok,真控制传输端到端) |
> | 4A | HID boot:Configure Endpoint + interrupt-in EP + SET_PROTOCOL + 报告解码 | f5a8fd5 | run-kernel-test-xhci **930/0**(configure endpoint ok 加 interrupt-IN EP;静止鼠标 NAK 无报告符合预期) |
> | 4B | 鼠标输入分层收拢(XhciSlot 退回通用+UsbMouse 类+drivers/mouse/)+ 注入 event_queue(dy=+hid_dy)+ 输入源互斥 | 8ff216f | run-kernel-test-xhci **930/0**(HID mouse -> boot ok 经 UsbMouse,分层重构行为不变)+ test_host 54/54 |
> | 5A | async interrupt-IN + TransferListener 分发 + UsbMouse listener + boot mouse 接线(usb_init.cpp) + make run 切 USB mouse | e043d89 | run-kernel-test 931/0 + run-kernel-test-xhci 931/0 + make run "HID boot mouse armed slot=1 ep1-IN" 无 panic + test_host 绿 |
> | 5B | USB boot keyboard(复用 async+TransferListener;keyboard/hid HID keycode→ASCII + 边沿检测 + UsbKeyboard + usb_init 双设备枚举 + make run usb-kbd) | 5238e10 | run-kernel-test 931/0 + run-kernel-test-xhci 931/0 + make run "keyboard armed port=4"+"mouse armed port=5" 无 panic + test_host 绿 |
> | 5C | 测试加固 + ROADMAP/PLAN/todo/notes 同步(本批:ROADMAP F5-M5✅ + todo 04-xhci 标实现 + 5B notes + memory) | (本次) | docs-only |

> **F1-M3 = DMA 基础设施 ✅ 完成（2026-06-16）**。
> **F1-M4 = 块设备抽象 ✅ 完成（2026-06-16）**。
> **F5-M1 = AHCI DMA 迁移 ✅ 完成（2026-06-16）**。
> **F2-M6 = ext2 Cache ✅ 完成（2026-06-17）**。
> **F2-M7 Buddy PMM ✅ 完成（2026-06-18，fresh KVM 742/0 + GUI 冒烟）**：buddy 伙伴系统替换 PMM flat bitmap（per-order bitmap free-list，非侵入式）。Bug1（direct-map reserved PF，批3）+ Bug2（WSL2 nested KVM 对侵入式 free-list 写读不一致，改 bitmap 解，GOTCHA#14）均修。**详见下方「F2-M7 Buddy PMM」段 + `document/notes/2026-06-17-f2-m7-direct-map-buddy-handoff.md`**。solid 基线 = main（M6 #12，734）。F2 进度 7/7 + M7b（M1-M7 ✅，**M7b SLAB ✅ 完成 2026-06-18**：kmalloc 全替 Heap + 专用缓存 Task/VMA/CachedPage，见下方「F2-M7b」段）。
> **FO 可观测性/调试基建 ✅ 完成（2026-06-18，763/0 + panic 冒烟）**：frame pointer + KALLSYMS lookup + 防御 backtrace + 统一 panic handler（收编 dump_registers/kpanic/fatal_halt + backtrace + memstats）+ dump_memory_stats。关键 GOTCHA：`CMAKE_BUILD_TYPE` 默认空(-O0)→ 首次 -O2 Release 验证全绿（建议 CI 加 -O2 门禁）；`VMM::translate()` 不支持 huge 页 → backtrace 改栈范围检查；kprintf 不支持 `%zu`。**M5 崩溃持久化推迟**（持久化层前提不满足）、**1b 真实符号注入 follow-up**（CMake 两阶段，裸地址+addr2line 降级）。详见 `document/notes/2026-06-18-fo-observability.md`。
> **F3-M1 信号系统 ✅ 完成（2026-06-18，5 批，763→783）**：核心 POSIX 信号（Signal/SigSet/SigAction）+ 投递（send/pick/check_and_deliver）+ kill/sigaction/sigprocmask/sigreturn + Custom handler round-trip（中断路径 + int $0x80 trampoline）+ 集成（PF→SIGSEGV/exit→SIGCHLD/write→SIGPIPE）。详见下方「F3-M1 信号」段 + `document/notes/2026-06-18-f3-m1-signals.md`。
> **F3-M2 线程支持（clone + futex + TLS）✅ 完成（2026-06-18，5 批，783→810）**：为 musl/pthread 打内核地基。①TLS(fs_base) ②futex(WAIT/WAKE/BITSET) ③共享 refcount 指针化(sig_actions/fd_table/cwd)+retrofit fork ④线程组+clone 核心(子进程用户栈返回 patch 帧 user_rsp,GOTCHA#18) ⑤cleartid exit 集成+libc wrapper。**关键踩坑 GOTCHA#17-20**（FS_BASE 规范地址/clone 用户栈返回/改接口致测试早返回悬垂/栈拷贝 full_used 下溢）。**真用户态线程 round-trip + 实机 GUI 冒烟 + AddressSpace refcount + futex timeout 留 follow-up**。详见文末「✅ F3-M2」段 + 各批 notes。下个焦点：F3-M3 进程组/会话（已启动，见下）。
> **F3-M3 进程组/会话 + waitpid 阻塞 ✅ 完成（2026-06-19，5 批，810→827）**：为 Job Control / TTY 打地基 —— pgid/sid/setpgid/setsid/killpg + 补 waitpid 阻塞（复用 futex 的 block/unblock + exit 唤醒父）。详见文末「✅ F3-M3」段。
> **F3-M4 调度器接口验证与增强 ✅ 完成（2026-06-19，5 批，827→840）**：①SchedulingClass 策略钩子(task_tick/task_fork/task_deadline,时间片抢占内聚到调度类,删 current_slice_) ②优先级感知 RoundRobin(pick_next 选 priority 最小者,同优先级 RR) ③多调度类实际查询(pick_next_from 数组原语,schedule/exit_current/run_first 不再绕过 classes_[]) ④SIGSTOP/SIGCONT 真调度(TaskState::Stopped 状态机 + signal_send 发送时恢复 + schedule 守卫排除 Stopped)。**向后兼容**(生产单类场景等价,827 回归全绿)。关键踩坑 GOTCHA#22(TaskBuilder 消耗全局 tid 计数器跨测污染)。**F3 进程与线程全里程碑收官(M1-M4)**。详见文末「✅ F3-M4」段 + `document/notes/2026-06-19-f3-m4-{1,2,3,4}-*.md`。
> 状态：✅ DONE / 🔄 NEXT / ⏳ PENDING / ⛔ BLOCKED。每批≈一 commit，完成门 `run-kernel-test` 全绿。

> **F-INFRA 基建加固 ✅ 完成（2026-06-19，10 批 12 commit，基线 840/0 全程绿、零警告）**：F2/F3 后复杂度陡增前夯基——CI 粘合/静态门禁/警告收紧/format 属性/static_assert/KALLSYMS 真符号/64 位 gdbinit+decode-trace/NotNull 指针契约/clang-tidy/UBSAN/lockdep。详见下方「✅ F-INFRA」段 + `document/notes/2026-06-19-finfra-{1..10}-*.md` + summary。

## ✅ F4-M1（ACPI 静态表解析）完成 — 2026-06-19

> F4（SMP）M1：从 BIOS ACPI 表提取 APIC 基址 + CPU 列表 + IRQ override → `ACPIInfo`，为 M2（APIC init）铺路。不做 AML/FADT/HPET 深度（HPET 留 F5-M4）。基线 main 840/0 → 859/0 + production GUI 冒烟。

| 批 | 范围 | Commit | 测试 |
|----|------|--------|------|
| M1-1 | RSDP 定位（EBDA+ROM + checksum）+ 单测 | faabce1 | 849/0 |
| M1-2 | find_table（RSDT 32位主路径 + 每表 checksum）+ 单测 | d862b01 | 854/0 |
| M1-3 | MADT→ACPIInfo（LAPIC/CPU/IOAPIC/IRQ override）+ 单测 | 5216839 | 859/0 |
| M1-4 | g_acpi_info + init() + main 接线 + 真机探针 + 收尾 | (本次) | 859/0 + GUI 冒烟 |

**总结**：RSDP 定位 → find_table（RSDT 32位 / XSDT byte-wise 读避 -Wcast-align）→ MADT 解析（packed struct + reinterpret 读变长 ICS → ACPIInfo）→ main.cpp 接线（PIT 后，direct-map 就绪）+ 启动探针。
**关键 GOTCHA**：①QEMU pc 默认 **ACPI 1.0 RSDP（rev 0，仅 RSDT 32位，无 XSDT）** → M1-2 走 RSDT 主路径（修正 propose 阶段"ACPI 2.0"假设）。②测试 log 含 ANSI escape → grep 当二进制静默，查日志用 `grep -a`。③ACPI 表（RAM）经 direct-map cache-enabled 访问 OK，与 APIC MMIO（M2 需 `VMM.map + FLAG_PCD` 禁缓存）严格区分。
**真机**：`[ACPI] 1 CPU(s), LAPIC 0xFEE00000, IOAPIC 0xFEC00000, 5 IRQ override(s), pcat=1`（QEMU pc 标准拓扑；5 override 含 IRQ0→GSI2，M2-3 消费）。详见 `document/notes/2026-06-19-f4-m1-{1,2,3,4}-*.md`。下个焦点：**F4-M2 LAPIC + IOAPIC**。

## ✅ F4-M2（Local APIC + I/O APIC + PIC→APIC 切换）完成 — 2026-06-19

> F4（SMP）M2：中断现代化——8259 PIC → LAPIC + IOAPIC。解锁现代中断（更多源 + 为 M3 多核 / xHCI INTx# 铺路）。基线 859/0 → 869/0 + production 真机（PIT 13 ticks under APIC）。

| 批 | 范围 | Commit | 测试 |
|----|------|--------|------|
| M2-1 | LocalAPIC（xAPIC MMIO + FLAG_PCD）+ mock | 8fcd5fc | 865/0 |
| M2-2 | IOAPIC（indirect + set_redirect + mask）+ mock | 58ed314 | 869/0 |
| M2-3 | IrqBackend::eoi + PIC→APIC 切换 + 改 5 处 EOI + 真机冒烟 | fcf1151 | 869/0 + PIT 13 ticks |
| M2-4 | 收尾（本文档 + ROADMAP；IPI/APIC timer 推迟 M3） | (本次) | — |

**总结**：LAPIC（xAPIC MMIO，qemu64 无 x2APIC）+ IOAPIC（indirect IOREGSEL/IOWIN + set_redirect 64-bit）+ IrqBackend 抽象（EOI PIC/APIC 派发）+ switch_to_apic（mask PIC / LAPIC init+enable / IOAPIC redirect IRQ0,1,12→0x20,0x21,0x2C 照 ISA override 查 GSI / flip backend）+ 改 5 处 EOI（pit/keyboard×2/mouse/irq_handlers×2）。
**关键 GOTCHA**：①APIC MMIO 必须 `VMM.map + FLAG_PCD`（禁缓存，**严禁 direct-map**）②freestanding `for(x:{...})` 需 `<initializer_list>`（改数组循环）③IOAPIC redirect 照 ISA override 查 GSI（QEMU IRQ0→GSI2，直映 IRQ0→GSI0 错→无 tick）④EOI 在 C handler 发（stub 不动，IrqBackend 一处切换）⑤qemu64 无 x2APIC（xAPIC MMIO 必选）。
**真机**：`[APIC] switched to APIC (LAPIC id 0, IOAPIC @0xFEC00000)` + **PIT 13 ticks under APIC**（IRQ0→GSI2→LAPIC vector 0x20 路由通）+ GUI 启动不崩。
**推迟 M3**：IPI（send_ipi/init/sipi）+ APIC timer（多核 AP 启动 / per-CPU timer 需要）；MSI/MSI-X → F4-M2b 或 F5-xHCI 前置。
**F4-M1+M2 完成**（9 commit，840→869/0 + 真机）。详见 `document/notes/2026-06-19-f4-m2-*.md`。下个焦点：**M3 AP 启动**（IPI + per-CPU）或 F4 暂停 push/PR。

## ✅ F4-M3 Phase 1（Per-CPU 架构,单核重构）完成 — 2026-06-19

> **Phase 1(P1-1~4)完成 + 合入 main(PR#21 squash `1cca2b9`)。** 含 console framebuffer 修复(`d61678e` 同 PR)。Phase 2(AP 启动)进行中,分支 `feat/f4-m3-ap-boot`(从干净 main 拉)。
> **设计文档(执行依据):[document/notes/2026-06-19-f4-m3-design.md](../notes/2026-06-19-f4-m3-design.md)** —— 调研结论、PerCpu GS 设计、9 批拆批、风险/GOTCHA。
> 核心:PerCpu{kernel_stack@0(兼容 syscall %gs:0), current, cpu_id, apic_id} + percpu_blocks[kMaxCpus] + percpu() 读 MSR_GS_BASE;context_switch 去 per-task GS;每 CPU GDT/TSS;AP trampoline @0x8000 + INIT-SIPI-SIPI(Phase 2)。
> 调研:SMP 就绪 ~20%,4 P0(current_ 静态/全局 runq/单 TSS/无 AP trampoline)+ 3 P1(futex/waitpid/mutex lost-wakeup,Phase 3 修)。**Phase 1 修了 P0#1(current→percpu)+ #3(单 TSS→gdt_blocks);#2 全局 runq / #4 AP trampoline 留 Phase 2/3。**

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| P1-1 | PerCpu 结构(kernel_stack@0)+ percpu_blocks[] + percpu() 静态返 [0];迁移 ~15 处 g_per_cpu;gs 页双镜像;测试改 percpu()->current;删 per_cpu.hpp/g_per_cpu | ✅ | eaccc57 | 869/0 + 真机 GUI |
| P1-2 | GS 锚定 PerCpu[0] + 完整 swapgs 纪律:usermode_init 设 GS_BASE/KERNEL_GS_BASE、删 gs 页;jump_to_usermode 加 swapgs;ISR 宏条件 swapgs(按 CS 判 CPL);context_switch.S 删 GS 存取(fs_base 保留);CpuContext gs/kgs 留 reserved、删 3 处 kgs_base=;percpu() 读 MSR_GS_BASE;补 msr.hpp;usermode_init 提前到 IDT 后 | ✅ | c1a511e | 869/0 + 真机 GUI |
| P1-3 | per-CPU GDT/TSS:g_gdt→gdt_blocks[kMaxCpus](不完整数组声明免耦合);tss_set_rsp0 内部读 percpu()->cpu_id(签名不变);main/main_test 用 [0] | ✅ | b9af79f | 869/0 + 真机 GUI |
| P1-4 | 收尾:全量 cmake --build + test_host + 真机 + ROADMAP/PLAN + Phase 1 收尾笔记 | ✅ | (本次) | 869/0 + test_host 全绿 + 真机 GUI |

> **P1-2 关键发现:原设计低估 swapgs 牵连**——ISR(interrupts.S)无 swapgs(仅 syscall.S 有),中断从用户态进入 GS_BASE=0 而 `schedule()→percpu()` 在中断上下文 → percpu() 读 MSR 会崩。改走**完整 swapgs 纪律**(Option A):ISR 宏按帧内 CS 判 CPL=3 条件 swapgs(entry RSP+144 / exit RSP+136,%rax scratch)。**usermode_init 提前到 IDT 后**(原在 sync 测试之后,P1-2 后会崩)。已知局限:NMI/#DB 在 syscall-exit swapgs 窗口(Linux paranoid 路径留 follow-up)。详见 `document/notes/2026-06-19-f4-m3-p1-2-swapgs-discipline.md`。
> P1-1 GOTCHA:gs 页双镜像(P1-1 过渡,GS 未动 → syscall 仍读 gs 页 → update_syscall_stack 双写 percpu+gs 页;P1-2 才并入);测试 `percpu()->current = t` 忠实迁移(非 set_current)。详见 `document/notes/2026-06-19-f4-m3-p1-1-percpu-block.md`。

## ✅ F4-M3 Phase 2(AP 启动 / SMP 双核)完成 — 2026-06-19

> **`-smp 2` 双核 AP online + idle 达成。** F4 最难里程碑(trampoline 首次实跑即通)。分支 `feat/f4-m3-trampoline`(从 main 拉,P2-1 已合 main PR#22)。3 commit 未 push。
> 设计依据:`document/notes/2026-06-19-f4-m3-design.md` P2-1~5 + plan 修订(4 处 gap)。
> **核对发现的 gap**:① 0x8000 是 bootloader stage2 加载地址(runtime 空闲,标准 SIPI 选址);② qemu.cmake 无 -smp(P2-4 加 run-*-smp target);③ AP boot 独立于 mini loader(trampoline 自建页表/长模式);④ 全局 runq(P0#2)P2 不修(AP idle)。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| P2-1 | LocalAPIC IPI:ICR(0x300/0x310)+ delivery mode 常量 + send_ipi/init/sipi(等 delivery status Idle)+ mock 单测 | ✅ | f4f5baf(PR#22) | 872/0(+3) |
| P2-4a | qemu.cmake 加 run-smp/run-kernel-test-smp(`-smp 2`) | ✅ | b8d9cb7 | run-kernel-test-smp 872/0 |
| P2-2/3/4b | trampoline `ap_trampoline.S` @0x8000(16→32→64,内联表达式寻址)+ ap_entry_long(切 CR3)+ ap_main(GS/GDT/IDT/LAPIC+屏障+cli;hlt)+ boot_aps(临时页表+拷贝注入+INIT-SIPI-SIPI+等就绪) | ✅ | 1194345 | **真机 -smp 2 AP1 online + GUI 稳定** |
| P2-5 | 收尾:-smp 1/2 回归 + 全量 + test_host + ROADMAP/PLAN(F4-M3 ✅)+ 笔记 | ✅ | (本次) | 872/0 + test_host + -smp 1/2 真机 |

> **P2-2 关键 GOTCHA(执行时踩/避)**:① **GAS 宏不内联展开**(`$TP()` 永远失败,`.altmacro` 也不行;本构建 GAS-direct `#`=注释非 cpp)→ 改**内联表达式 `(label-ap_trampoline_start+0x8000)`**(GAS 两遍解析同 section 符号差=常量)。② **CR3 切换不能在 0x8000**(切后内核 PML4 不映 0x8000)→ 在 higher-half `ap_entry_long`(两边都映)里切 + 设栈。③ AP `cli;hlt`(非 sti;hlt):`Scheduler::current_` 仍静态,AP 上跑中断 handler 有并发风险 → IF=0 永久 halt 归零(P2);M4 改可唤醒 + per-CPU 化。④ 双 SIPI 致 trampoline 跑两次(SIPI#1 后 AP init 慢,守卫发 SIPI#2 兜底,spec 行为,无害)。详见 `document/notes/2026-06-19-f4-m3-p2-ap-boot.md`。
> **边界**:AP idle 不跑用户任务(M4 多核调度:per-CPU runq + `current_` per-CPU);lost-wakeup 未修(Phase 3)。**F4-M3 全里程碑(Phase 1+2)收官。**

## ✅ F4-M4（多核调度）收官 — M4-0/1/2-1/2-2/2-3/3 全完成 — 2026-06-20

> M4 让 AP 从 idle 变成能跑用户任务。顺序 A(用户决策:先 M4-3 后 M4-2 + 不做 timer 抢占)。已完成 M4-0/M4-1(批0+1)+ **M4-3(prepare-to-wait,批3,`b33264b`,875/0)** + **M4-2-1(reschedule IPI 通路,`e8f0136`)** + **M4-2-2(per-CPU idle + runqueue 多核安全 + AP1 sti;hlt 基础设施,`3d080a0`,限定交付)** + **M4-2-3(迁移 GP 根治 + AP 真跑 user task,本批)**。**F4-M4 全里程碑收官**。详见 notes:`2026-06-20-f4-m4-0-1-test-refactor-percpu.md`、`2026-06-20-f4-m4-3-prepare-to-wait.md`、`2026-06-20-f4-m4-2-1-reschedule-ipi.md`、`2026-06-20-f4-m4-2-2-ap-idle-loop.md`、`2026-06-20-f4-m4-2-3-migration.md`。
> **上一轮试做 M4-1 回退教训**:phantom-task 测试(test_sync/futex/clone)靠 `current_`(静态)与 `percpu()->current` 的**未文档化分歧**让 block() 不 schedule;M4-1 改读 percpu 后必 hang。**真正 hack 是这条分歧,不是 role-play 本身。**

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| M4-0 | phantom 测试显式化:`NoRescheduleGuard`(gate block() schedule,生产 depth=0 无影响)+ `percpu()->current=X`→`set_current(X)`(GOTCHA#21)+ `RoundRobin::clear()`/`init()` 清 runq(test 隔离)+ `test_block_dispatches_to_runnable`(真 dispatch 路径覆盖) | ✅ | ac92bef | 873/0(+1) |
| M4-1 | `current_` 静态全局→per-CPU:删成员,`current()`读`percpu()->current`,set_current/schedule/exit_current/run_first/tick/yield/block 全走 percpu,删双写;~35 调用点经 accessor 零改 | ✅ | 7dba770 | 873/0 + smp 873/0 + host 48/0 + run-smp AP1 online/GUI |
| M4-2-1 | reschedule IPI 基础设施:vector 0xE0 + stub + handler(纯 `g_lapic.eoi()`)+ irq_init 注册 + `wake_idle_ap()`(多发 IPI 给在线 AP,免精确 idle 跟踪)+ `add_task`/`unblock` 唤醒点。单核 no-op(无在线 AP) | ✅ | e8f0136 | 875/0(单核 no-op,TCG) |
| M4-2-2 | per-CPU idle(`idle_tasks_[kMaxCpus]`+`idle()`/`setup_ap_idle`)+ runqueue 多核安全(`pick_next` 删 re-enqueue + `schedule` enqueue prev,4 RoundRobin 单测适配)+ AP1 `sti;hlt` 基础设施(ap_main 启动 idle 序列 + `ap_idle_entry` sti;hlt)。**限定交付:user task 迁移 GP(GOTCHA#25)留 M4-2-3** | ✅ | 3d080a0 | 875/0 + -smp 2 干净(AP1 online+GUI,无 GP) |
| M4-2-3 | **user task 多核迁移**:① `%gs` 安全首错捕获加到 `handle_gp`(outb/rdmsr/栈 frame,不碰 %gs,永久加固)→ 抓到首错 RIP=`schedule()` `prev->state` 解引用,rax=`0xf000ff53...`(BIOS 垃圾),gs_base=0;② 根因=[gdt.cpp `GDT::load()`](../../kernel/arch/x86_64/gdt.cpp) 重载 `%gs` 平坦段清零 `MSR_GS_BASE`(ap_main 先设 GS 后加载 GDT),**非 %gs 损坏**(M4-2-2 现场判断被递归污染误导);③ 修:移除 `%fs`/`%gs` 重载(长模式基址由 MSR 管);④ 启用 `ap_idle_entry` schedule+sti;hlt 迁移循环。AP 真跑 user task 收官 | ✅ | (本批) | 875/0 + 全量 host + -smp 2 干净(全启动 GUI+AP1 真跑+无 GP/死锁) |
| M4-3 | lost-wakeup prepare-to-wait:`prepare_to_wait`(irq_guard 下原子设 Blocked)+ `schedule_blocked`(NoRescheduleGuard 感知)+ unblock 幂等(仅 Blocked 才入队);Mutex/Sem/futex/waitpid 四处改用 prepare+schedule_blocked,block() 保留原样(测试/管理) | ✅ | b33264b | 875/0(+2 精确交错单测) |

> **关键 GOTCHA(M4-1)**:`fxrstor current()->fpu_state` 在任务恢复点必须读 percpu(= 切到本任务那次 schedule 设的值),**绝不能用局部 `next->fpu_state`** —— context_switch 返回时跑在 next 上下文,但执行的是 prev 当初被切出时那条 fxrstor(prev 栈帧里 next 已过期),旧全局 `current_` 读对,M4-1 须用 `current()` 等价。详见 GOTCHA#23。
> **不变量**:单核全程不变(percpu[0] 等价旧 current_);AP1 现真跑 user task(M4-2-3 迁移 GP 已根治:GDT::load 移除 %fs/%gs 重载)。**F4-M4 收官:per-CPU idle + runqueue 多核安全(pick 移除)+ reschedule IPI + lost-wakeup prepare-to-wait + 真 user-task 迁移。**

## ✅ F4-M5（同步原语）收官 — R3 + Batch2 分析 + R6-Part2 全完成 — 2026-06-20

> F4-M4 让 AP 真跑线程后,把 F3-M2「共享 refcount 指针化」对象做成真 SMP 安全,补 lockdep 锁序图。F-INFRA 划归(R3 原子引用计数 / R6-Part2 锁序图)+ M4 follow-up(waitpid SMP,经分析无需)。分支 `feat/f4-m4-2-3-migration`(叠 M4-2-3,前置未 push)。**F4 SMP 全域 M1-M5 收官**。计划 `.claude/plans/temporal-jumping-hartmanis.md`。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| M5-1 (R3) | **原子引用计数**:SharedCwd + SharedSigActions acquire/release → `__atomic_add/sub_fetch`(ACQ_REL,去 racy `>0` 守卫)。**FDTable 核对已 `lock_.guard()` 保护,跳过**(范围修订)。F4-M4 实多核后真并发的 use-after-free/leak 根治 | ✅ | 86f8071 | 875/0 + 全量 host + -smp 2 冒烟干净 |
| M5-2 (waitpid) | ~~waitpid children 链表加锁~~ **经分析不必要**:`Task::children` 每 Task 私有(CLONE_THREAD 是 sibling 不入 children,fork/clone 加到 `current()->children`,waitpid 扫 `current()->children`,exit 不碰 parent->children,无 reparent)→ 每链表只被拥有任务碰(单 CPU 时刻),无跨核链表访问;唯一跨核 datum 是 child->state(Zombie)x86 原子 + exit unblock 重扫覆盖。process_new.cpp 注释已订正 | ✅(无需) | 74d90a3 | — |
| M5-3 (R6-Part2) | **lockdep 锁序图**:`kernel/proc/lockdep.{hpp,cpp}` per-CPU 持锁栈(修 Part1 全局深度计数 SMP bug)+ 全局锁序邻接图 + DFS 检 AB-BA 环 + 递归锁检测;edge 表 irq-safe 原始自旋防 IRQ 重入。Spinlock::acquire/release + schedule() 断言接入。opt-in `CINUX_LOCKDEP`(默认 OFF 零开销) | ✅ | (本批) | CINUX_LOCKDEP=ON 875/0 零误报 + AB-BA spike 检出(panic)+ 默认 OFF 875/0 + 全量 host 零警告 |


## 🔄 F-QA（质量收敛与加固）— 2026-06-20 立项

> 横切里程碑（接 F-INFRA，插 F5 前）。来源：2026-06-20 一个 6-agent workflow（Linux 全谱 / 现代 C++ 工具链 / 维度 gap / 仓库实证 / 综合 / 对抗 critic）+ 用户四决策。两条腿——**防新债**（门禁 + deterministic 方法论 + 类型不变量）+ **抓存量**（系统审计坐实潜在问题）+ **修头号高危债**。基线 main 875/0（F4 SMP 收官 #24）。分支 `feat/f-qa`（从干净 main）。
> 用户决策：①强检查只要真抓问题的（仪式性废）；②禁异常铁律优先，属性尽力而为；③新维度加；④全包 Q1-Q4 + RefCount/UserPtr 都入本 F。
> 验证：每批 `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` 绿才提交；改公共接口/InodeOps/mock 补全量 `cmake --build build`；Q4 高风险加 `-smp 2` + UBSAN/LOCKDEP。

### ✅ Q1 完成（2026-06-20，6 commit，875/0 全程绿 + 零警告）

| 批 | 范围 | Commit | 产出 |
|----|------|--------|------|
| 批0 | 质量文档分层 + F-QA 立项 | 8bdfaa2 | DIRECTIVES L9 / QUALITY-GATES / prompts / todo/quality + ROADMAP/PLAN |
| Q1-1 | 零成本警告门禁(7 个 -Werror=) | 3f31a3e | vla/fallthrough/undef/duplicated/logical/format-security + DEBT-015(9 处栈帧) |
| Q1-2 | ErrorOr class `[[nodiscard]]`(子模块) + test -Wno | 7825710 | expected.hpp(子模块 af80a68) + DEBT-016(32 处 test 忽略) |
| Q1-3 | CI kernel-tests 6-config matrix | 20b624b | {Debug,Release}×{none,ubsan,lockdep} 并行 |
| Q1-4 | 测试项数单调不降门禁 | d582a72 | check_test_count.sh(baseline 875) |
| Q1-5 | host ASAN/UBSAN/gcov 基建 | f938335 | CINUX_HOST_ASAN option + **首跑抓到 DEBT-017 ring_buffer OOB** |

**Q1 收官**：防新债基座落地——编译门禁 + ErrorOr 铁律强化(`[[nodiscard]]`) + CI 多 config 常驻 + 测试项数门禁 + host ASAN 基建。ASAN 首跑即抓 `ring_buffer::push_batch` global-buffer-overflow(DEBT-017，P1，production pipe/keyboard 在用，单核串行潜伏)。3 新债(DEBT-015/016/017)登记。ci.yml host ASAN 待 DEBT-017 修后 flip 启用。下个：Q2 deterministic 审计方法论。

### ✅ DEBT-017 闭环（host ASAN findings）— 2026-06-20，feat/f-qa-q2

> 交接文档建议 Q2 主线前优先修的 production-adjacent bug。**诊断订正**：DEBT-017 原登记「`RingBuffer::push_batch` 边界 bug」**误诊**——push_batch 对 `buffer_` 自身安全(`tail_%N` + `!full()`)，越界是调用方传错 count；真因**不在 push_batch、不在 Cinux-Base 子模块**。复现 ASAN 拿 ground truth 后定位 4 处 3 类，全在主仓库：

| # | 位置 | 类型 | 修 |
|---|------|------|----|
| 1 | `test/unit/test_pipe.cpp:458` | OOB（`try_write("BBBB",200)` 读 5B 字面量越界） | 真实 200B buffer |
| 2 | `test/unit/test_fd_table.cpp`（776 处） | 泄漏（栈 FDTable 不 release → File 不释放） | **`kernel/fs/file.cpp` 加 `~FDTable()` 兜底释放**（release 路径幂等 + production 防御） |
| 3 | `test/unit/test_multi_terminal.cpp:745` | 泄漏（`add_window` 失败不接管 + 未 delete） | overflow 后 delete |
| 4 | `test/unit/test_sys_pipe.cpp`（9 处） | double-free（析构暴露：`set()` 接管所有权，test 误手动 delete） | 删 9 处手动 delete（归析构），保留被 replace 旧 File |

> **production 零影响**：push_batch production chunk 守护安全；FDTable production 经 release/close 释放（析构幂等 no-op）；sys_pipe File 归 fd_table 不手动 delete。**残留异味**（登记非修）：`test_shell_redirect` `~PipeRedirect` 的 `delete stdin_file` 侥幸安全（构造局部变量 shadow 同名私有成员，成员恒 nullptr，delete nullptr no-op；消除 shadow 即 double-free）。
> **验证**：host ASAN 全绿（Debug + Release+ASAN+UBSAN+FORTIFY CI 对等，全量 `make test_host` 100%）+ `run-kernel-test` 875/0 + 零警告。**ci.yml host-tests flip `-DCINUX_HOST_ASAN=ON` 硬门禁**（Q1-5 留的开关兑现）。详见 `document/notes/2026-06-20-f-qa-debt017-host-asan-fix.md`。下个：**Q2 deterministic 审计方法论**。

### ✅ Q2 完成（deterministic 审计方法论）— 2026-06-21，feat/f-qa-q2

> 把 `audit-guide.md` 从叙述式「看什么/搜索/红线」改造为 per-维度 **deterministic 四段式**（A 锚点 / B 不变点 / C 门槛 / D 闭环），让任意两轮审计可比较（根治发散）。2 批：

| 批 | 范围 | Commit |
|----|------|--------|
| Q2-1 | 范式骨架(§0/§1+§1.1 模板) + D4 完整样板 + D13/D14 新增(真实符号) | 37a1332 |
| Q2-2 | D1-D3/D5-D12 四段式(D5/D8 标「先读码后锚点」例外) | (本次) |

> **范式**:rg 命令放 `sh` 代码块(避免表格 `|` 冲突),命中表只记锚点+命中数(机器数,非 yes/no);不变点逐条 pass/fail/n/a + file:line;非债须反例;DEBT 去重(已登记只补未覆盖不变点);锚点可回归(下轮 diff>0 须说明)。D5/D8 例外(有效 rg 需先读调用链,GOTCHA#25/26)。
> **关键校正**:D13/D14 锚点先 rg 校准真实符号(`FDTable::alloc`/`PidAllocator::alloc`/`g_pmm.alloc_page`/`e_phnum`/`static_cast<size_t>`),非前轮虚构的 `alloc_fd`/`request_irq`(零命中)。D4 样板 5 锚点 + D13/D14 全部 rg 可跑命中>0。顺手坐实 `kMaxCpus` 不一致(acpi `size_t=16` vs percpu `uint32_t=8`,类型也不同)→ D13 Q3 首审线索。
> **验证**:文档-only(R0);14 维度齐全(D1-D14)+ 叙述式残留 0 + 抽样锚点全可跑。debt.md 审计计划 12→14 维度。详见 `document/notes/2026-06-21-f-qa-q2-deterministic-audit.md`。下个:**Q3 系统审计**(用 Q2 方法论审 D4/D5/D6/D7/D11 + D13/D14)。

### ✅ Q3 完成（系统审计，14/14 全审）— 2026-06-21，feat/f-qa-q2

> 用 Q2 deterministic 方法论审全 14 维度（5 批：Q3-1 D4+D13 / Q3-2 D5+D6 / Q3-3 D7+D11 / Q3-4 D14+D9 / 收尾 D1+D8+D10+D12）。零风险只读。报告 `document/todo/quality/reports/2026-06-21-d*.md`（5 份）。

| 维度 | 结论 | 债 |
|------|------|----|
| D4 进程生命周期 | **fail** | **DEBT-002 精确坐实（P1 头号）**：production Task 退出无 `delete`/`release_resources`（`remove_task` 仅 test 调用）→ Task+sig_actions/fd_table/cwd+核栈+addr_space 泄漏 |
| D5 调度/迁移 | pass | F4 SMP 清洁（GOTCHA#23/25/26） |
| D6 用户边界 | warn | DEBT-019（P3 用户指针非 copy，PF 兜底）+ DEBT-012 关联 |
| D7 错误韧性 | pass | FO 清洁（panic 仅不变量） |
| D9 静态工具 | pass | F-INFRA/F4-M5/Q1 清洁（host-ASAN 硬门禁） |
| D11 模块组织 | pass | 源全 <500 + check_line_limits 排除 test/ |
| D13 资源配额 | fail | **DEBT-018（P2 kMaxCpus 不一致）** |
| D14 整数溢出 | fail | **DEBT-020（P3 ELF 字段算术）**+ DEBT-012 关联 |
| D1/D8/D10/D12 | pass/warn | 架构铁律/commit 规范清洁；D8 warn=已知 GOTCHA#11 盲区 |

> **Q3 总结**：新增 3 债（DEBT-018/019/020）+ DEBT-002 精确坐实（P1，Q4 头号目标）。6 维度 pass（F4/FO/F-INFRA/架构清洁）证实前期里程碑扎实。deterministic 方法论首次全量实战（锚点 rg → 读码 → pass/fail 证据 → 登记），可复现。**喂 Q4**：修 DEBT-002 exit cleanup + DEBT-006 AddressSpace refcount（最险，单独 propose）→ DEBT-001/003/004/005。

### ✅ Q4a 完成（RefCount + UserPtr 类型先行）— 2026-06-21，feat/f-qa-q4

> Q4 头号高危债收敛里程碑第一批。类型不变量为 Q4b-e 消费者（SharedCwd/FDTable/AddressSpace/Task）铺路，**纯铺路不碰 fork/execve/PF/validate_user_ptr**。propose 走三重验证后动手（spike + 联网核验 Linux refcount_t + 7-lens 对抗审查）。分支 `feat/f-qa-q4`（从干净 main `d708e95`）。详见 `document/notes/2026-06-21-f-qa-q4a-types.md`。

| 批 | 范围 | Commit |
|----|------|--------|
| 批1 | `refcount.hpp`（Cinux-Base 子模块，`__atomic_*` 饱和语义对齐 Linux refcount_t）+ 主仓 bump + `test/unit/test_refcount.cpp`（10 单测） | 子模块 `e5f6e10` + 主仓 `f8ce80c` |
| 批2 | `user_ptr.hpp`（kernel/lib，zero-overhead 标记对齐 sparse `__user`）+ `kernel/test/test_user_ptr.cpp`（7 单测） | 主仓 `50c83bb` |

**关键决策**：① RefCount 用 `__atomic_*` 内建（**spike 否决 std::atomic**——编译过但拖 libstdc++ `__glibcxx_assert_fail` 符号，kernel `-nostdlib` 链接失败）；② 饱和 `kRefcountSaturated=INT_MIN/2 (0xC0000000)`（**联网核验订正**：离 0/INT_MAX 等距防 race 漂移，非 INT_MIN；inc RELAXED fetch-then-clamp，dec RELEASE old==1→true+acquire fence）；③ UserPtr 不内置 validate（避免 kernel/lib→syscall 反向依赖，validate 行为不变留 DEBT-019）；④ RefCount 单测放主仓库 `test/unit/`（**BLOCK 纠正**：子模块 `test/` 主仓 CI 不构建——`third_party/CMakeLists.txt` 不 add_subdirectory(Cinux-Base)）。
**边界（refcount.hpp @file）**：服务堆对象生命周期（非物理页 mapcount DEBT-003 独立 int16）；acquire(RELAXED) 不提供字段可见性；简单生命周期无 on-zero 清理 hook（FDTable 迁移外移）。
**验证**：test_host 全绿（refcount 10/10）+ run-kernel-test 875→**882/0**（UserPtr 7/7，ALL PASSED）+ make run GUI 桌面（[APIC]+[GUI] Desktop，无 panic）+ 全量编译零警告。
**下一步**：Q4b DEBT-003 CoW mapcount（独立 per-page int16，**不用 RefCount**）/ Q4c DEBT-001+004 / Q4d DEBT-005 / Q4e DEBT-002+006（RefCount 首个真消费者 + 最险，碰 fork/execve/PF，单独 propose）。

### ✅ Q4b-e 完成（头号高危债收敛,DEBT-001/002/003/004/005/006 全修）— 2026-06-21，feat/f-qa-q4

> Q4a 类型先行后,Q4b-e 修 6 债（4-agent workflow 调研 + 用户「全连做」）。9 批 + 1 收尾 fix。详见 `document/notes/2026-06-21-f-qa-q4-debt-convergence.md`。

| 批 | 债 | commit |
|----|----|--------|
| Q4d | DEBT-005 PidAllocator 锁 | 389987c |
| Q4c-1 | DEBT-004 sys_exit 无条件 unblock | 7b72659 |
| Q4c-2 | DEBT-001 registry 锁 + killpg 释锁后 send | 928b645 |
| Q4b-1/2/3 | DEBT-003 CoW mapcount（PMM 元数据+fork/execve+cow fault+单测） | 0a4ba1c/34a4595/037a08d |
| Q4e-1 | DEBT-006 AS RefCount（CLONEVM acquire） | 7ddda74 |
| Q4e-2 | DEBT-002 waitpid reap delete Task+核栈+AS release | 3983fe6 |
| Q4e-3 | DEBT-002 exit_current deferred-free（scheduler reap） | 4bb6ca4 |
| Q4e-2 fix | reap 补 signal_unregister（防 registry 悬垂） | e6ce2f4 |

**验证矩阵全绿**：run-kernel-test 887/0（+5 mapcount）+ **-smp 2** 887/0（SMP 债）+ **LOCKDEP** 887/0（锁序无误报）+ host ASAN test_host 绿 + 实机 GUI（kernel_init exit→reap 不崩）。
**关键设计**：核栈自释放 deferred-free（Q4e-3,exit_current 在自己核栈）+ CLONE_VM AS refcount 到 0 才 delete（Q4e-1/2）+ killpg 释锁后 send（Q4c-2,防 lockdep）+ CoW mapcount 防 fork+exec UAF（Q4b）。
**残留 follow-up**：SMP 跨核 TLB shootdown（cow free old）/ registry find TOCTOU（Q4e 修 delete 后）/ 孤儿 reparent —— 当前 -smp 2 AP idle 不触发,登记。

**F-QA 里程碑收官**（Q1 防新债门禁 + Q2 审计方法论 + Q3 系统审计 + Q4 头号债收敛）。用户痛点「内存偶现挂死/四处崩溃」头号燃料（进程生命周期引用计数洼地）收敛。

### 里程碑骨架

| M | 名称 | 批概要 | 风险 |
|---|------|--------|------|
| Q1 | 零成本门禁+CI 矩阵 | 编译器警告门禁包(`-Wframe-larger-than=1024`/`-Wvla`/`-Wimplicit-fallthrough=3`/`-Wundef`/`-Wduplicated-branches`/`-Wduplicated-cond`/`-Wlogical-op`/`-Wnull-dereference`/`-Wformat-security` + `-Werror` 子项); `[[nodiscard]]` 系统化于返回 `ErrorOr` 的 public 函数; CI 多 config 矩阵(`{Debug,Release}×{default,UBSAN,LOCKDEP}`); 测试项数单调不降门禁; host ASAN/TSAN/gcov | 低(纯 CMake/CI/属性) |
| Q2 | deterministic 审计方法论 | audit-guide 改造 per-维度 deterministic checklist(D4 样板 + D5/D8 "先读码后定锚点"例外); D13 资源配额/D14 整数溢出 新增(**先 rg 校准真实符号**); QUALITY-GATES↔audit-guide 范式统一; 工作树质量文档 commit | 极低(文档) |
| Q3 | 系统审计抓潜在问题 | 新方法论审 D4/D5/D6/D7/D11 + D13/D14; `reports/`+DEBT-015+; D2/D3 的 9 条 ⚠️ 坐实或降级 | 零(只读) |
| Q4 | 头号高危债收敛 | `RefCount`(Cinux-Base)/`UserPtr`(kernel/lib) 类型; DEBT-003 CoW mapcount(RefCount 首个消费者); DEBT-001 registry 锁 + DEBT-004 waiting_for_child; DEBT-005 PidAllocator 锁; DEBT-002 exit cleanup + DEBT-006 AddressSpace refcount(联动,Q4e 最险) | 高(fork/execve/PF + 子模块) |

**执行序**：Q1（低风险快赢）→ Q2（方法论）→ Q3（审计坐实,喂 Q4）→ Q4a(RefCount/UserPtr 类型) → Q4b/c/d(修坐实债) → Q4e(exit cleanup,最险,做到时单独 propose)。

### 风险重点
- Q4 碰 fork/execve/PF 核心路径 + Cinux-Base 子模块(`RefCount` 跨仓库,走子模块 PR)。
- `[[nodiscard]]` 加在 `ErrorOr` class 上最强但跨子模块; Q1 先 kernel/ public 函数级, `ErrorOr` class 级留 Q4a 碰子模块时顺带。
- Q4e(exit cleanup)体量最大, 做到时单独 propose 评估是否拆独立 F。
- 核栈 16KB(linker.ld), `-Wframe-larger-than` 阈值定 1024(留中断嵌套余量)。

## ✅ F-INFRA（基建加固）完成 — 2026-06-19

> 横切里程碑（像 FO，插 F4 SMP 前）。目标：把调试/静态检查/指针语义/CI 粘合从"靠自觉"升级为"机器可见 + CI 强制"，让 UB/悬垂指针/并发死锁/隐式窄化在非确定性到来前被抓住。对齐用户铁律"可调试优先于性能"。
> 来源：2026-06-19 一个 26-agent workflow（6 维代码审计 + 5 维联网调研 + 综合 + 对抗验证 + 完整性审查）的验证结论；记忆 `infra-hardening-investigation.md`。
> 约束：C++17 freestanding/无异常/无 RTTI；Cinux-Base 零堆/零 OS 耦合；内核单核（并发真修复 R3 原子引用计数、R6-Part2 锁序图 DFS 划 **F4-M5**）。基线 840/0（F3-M4 收官，PR#19 合 main）。
> 验证：每批 `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` 绿才提交；改公共接口/InodeOps/mirror 补 `cmake --build build -j$(nproc)` 全量（CI 盲区：run-kernel-test 不编 test/unit/）。

| 批 | Tier | 范围 | 状态 | Commit | 测试 |
|----|------|------|------|--------|------|
| I-1 | 0 | CI 防挂死（timeout 包裹 run-kernel-test）+ 失败上传串口日志（G1/G8） | ✅ | (本次) | 840/0 |
| I-2 | 0 | check_freestanding_headers.py + 修 icon_data.hpp `<array>` + CMake GCC 版本断言（G9/G2） | ✅ | (本次) | 840/0 |
| I-3 | 0 | 警告标志收紧（-Wshadow/-Wold-style-cast/-Wnon-virtual-dtor/-Woverloaded-virtual/-Wformat=2 + -Werror=return-type）+ 清理（17 cast + 15 预存警告 + build-id）→ 零警告构建（R2） | ✅ | (本次) | 840/0 |
| I-3b | 0 | kprintf/kvprintf/kpanic 加 `__attribute__((format))` + 清理 21 处真实格式不匹配（R2b） | ✅ | (本次) | 840/0 |
| I-4 | 0 | static_assert 布局锁：SlabHeader(40)/SlabCache(64)/LogEntry(272)/VMA(56) + 两处 InterruptFrame offsetof 矩阵(168)（R11）+ mini 链接零警告 | ✅ | (本次) | 840/0 |
| I-5 | 1 | KALLSYMS 真符号注入：nm POST_BUILD 生成表（big_kernel + big_kernel_test）+ boot 注册（R4） | ✅ | (本次) | 840/0 |
| I-6 | 1 | .gdbinit 64 位长模式重写（无偏移 file big/big_kernel）+ decode-trace.sh addr2line demangle + 修 run-gdb 路径（R9/G5） | ✅ | (本次) | 840/0（无内核改动） |
| I-7 | 2 | NotNull<T> 进 kernel/lib（精简 gsl，裸 assert，零开销）+ scheduler 5 永不为 null 静态入参采纳；set_current 抓出为 nullable 留 Task*（R5） | ✅ | (本次) | 840/0 |
| I-8 | 2 | .clang-tidy 精选 allowlist（advisory 本地，不加 CI 门禁——版本偏移教训）；实测抓到 scheduler.cpp:152 null-deref（R8） | ✅ | (本次) | 配置/无内核改动 |
| I-9 | 3 | UBSAN freestanding 桩：CINUX_UBSAN（opt-in OFF 默认），GCC void* builtin 签名（不抄 SerenityOS）+ invalid_builtin，桩调 kpanic，排除 Cinux-Base/诊断路径；默认 840/0、UBSAN 构建也 840/0 零命中、smoke fire+backtrace（R1） | ✅ | (本次) | 840/0 |
| I-10 | 3 | lockdep-Part1：held_spinlock_depth + schedule() 入口断言（CINUX_LOCKDEP opt-in，默认 OFF）；双构建验证 OFF 840/0 + ON 840/0 无误报（R6） | ✅ | (本次) | 840/0 |

划归 F4-M5（不在本里程碑）：R3 原子引用计数（SharedCwd/SharedSigActions）、R6-Part2 锁序图 DFS。
follow-up（渐进，不拆批）：R7 BUG_ON/WARN_ON + CODING-TASTE 补 assert-vs-Error 判据、R10 mini-KASAN 红区、R12 next_tid 测试复位、R13 -O0 CI 矩阵、G3 确定性种子、G4 xfail 标记、G7 分层 include 检查、G10 启动阶段计时。

## ✅ M2（内核日志）已完成 — 2026-06-16

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | ConcurrentRingBuffer（kernel/lib/，MPSC irq_guard Spinlock）+ 测试 | ✅ | 974e406 | 667/0（+5）|
| 批2 | KernelLog（LogEntry ring + klog_*宏 + kprintf 攒行 sink）+ 测试 | ✅ | d2936a6 | 671/0（+4）|
| 批3 | sys_dmesg（SYS_dmesg=103，格式化历史读取）+ 测试 | ✅ | 4b3b95f | 674/0（+3）|
| 批4a | KernelLog::log 加实时 console 输出（reentrancy guard 避双重） | ✅ | cbcbb3a | 674/0 |
| 批4b | exception_handlers [FATAL]/[EXCEPTION] → klog_error/warn | ✅ | 809bf7d | 674/0 |
| 收尾 | 文档(本文+ROADMAP+todo+document/notes) + 全量 run-kernel-test | ✅ | (本次) | 674/0 |

dmesg 全链路闭环：`kprintf`/`klog_*` → KernelLog ring（IRQ 安全）→ `sys_dmesg` 格式化 `[LEVEL] tick: msg` 给用户态。ConcurrentRingBuffer 落地（M1 推迟的 MPSC 封装）。`klog_*` 经批4a 实时 console + ring；exception 高价值 error 迁 `klog_error/warn`（API 统一）。`kprintf` 全量迁移（294 除 mini）留后续渐进。662 → 674（+12 新测试）。

## ✅ F1-M3（DMA 基础设施）已完成 — 2026-06-16

> 目标：设备无关 DMA 基建，收编 ad-hoc（PMM + VMM + 硬编码 phys→virt 偏移 `0xFFFFFFFF80000000ULL`）。下游契约 = F5-M1 AHCI（`DmaPool.alloc()→DmaBuffer` + `PrdtBuilder`）。
> 决策：PrdtBuilder 纳入 M3（批3）；归属 `kernel/drivers/dma/`；phys→virt 用 `VMM.map`（批2 定）。
> 不碰 `ahci.cpp`(F5-M1)/`IBlockDevice`(M4)；不动早期启动 ad-hoc（PMM 就绪前用不了，OPEN GOTCHA #5 同类启动序约束）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | DmaBuffer（move-only，phys/virt/size，RAII release 回调）+ 测试 | ✅ | 49b7413 | 681/0（+7）|
| 批2 | DmaPool（`ErrorOr<DmaBuffer>`，封装 PMM+VMM，复用 direct-map 永久映射）+ 测试 | ✅ | fd65b4c | 687/0（+6）|
| 批3 | PrdtBuilder（设备无关 scatter-gather segment 构建器）+ 测试 | ✅ | 6426417 | 694/0（+7）|
| 批4 | 收尾：memory_layout.hpp 注释语义化 + M3 总结 + 全量验证 | ✅ | (本次) | 694/0 |

**完成总结**（662→694，M3 +20）：DMA 三件套落地——`DmaBuffer`（move-only 句柄，phys/virt 配对，RAII release 回调）+ `DmaPool`（`ErrorOr<DmaBuffer>`，封装 PMM+VMM，复用 direct-map 永久映射，免 virt 分配器）+ `PrdtBuilder`（设备无关 scatter-gather segment 构建器）。架构：A.6 `ErrorOr`（PMM/VMM bool→Error 转译）；A.7 不入 Cinux-Base（依赖 PMM/VMM）；命名空间 `cinux::drivers::dma`。关键教训 GOTCHA #7（direct-map 勿 unmap）。下游 F5-M1 AHCI 契约就绪（`g_dma_pool.alloc()` + segment→`HBAPrdtEntry`）；ahci.cpp 迁移留 F5-M1，`IBlockDevice` 留 M4。

## ✅ F1-M4（块设备抽象）已完成 — 2026-06-16

> 目标：最小化同步 `IBlockDevice` 接口，解耦 ext2 与 AHCI，收编 ext2 自有 ad-hoc DMA（`g_pmm.alloc_page + g_vmm.map(EXT2_DMA_VIRT_BASE)`，M3 同类遗留）。
> 决策：接口走 `ErrorOr<void>`（纯内核内部，A.6；`Error::IOError` 已就绪），不沿用 todo 草案 bool；ext2 内部 `read_block` 批3 暂保 bool（渐进迁移同 M0 FS 层），仅 `dev_->read_blocks` 用 ErrorOr，避免 ~20 处调用点同改签名撑爆批3。
> 含 `AHCIBlockDevice` 薄适配器（M4 闭环真机验证，**不碰 `ahci.cpp` 本体**——内部 DmaPool/PrdtBuilder 重构留 F5-M1）。不引入请求队列/异步 I/O、Page Cache（→F2-M4）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `IBlockDevice` 接口（`kernel/drivers/block_device.hpp`，`ErrorOr<void>`）+ `RAMBlockDevice` 测试桩 + 单测（读写 round-trip / block_count·size / 越界） | ✅ | 0d48abf | 701/0（+7） |
| 批2 | `AHCIBlockDevice` 适配器（持 `DmaBuffer`/M3 `g_dma_pool`，不碰 ahci.cpp）+ 真机单测（读 sector 0） | ✅ | 975582b | 705/0（+4） |
| 批3 | Ext2 `AHCI&,port`→`IBlockDevice*` + 淘汰 `dma_buf_phys_/virt_/dma_ready_/ensure_dma_buffer` + ~20 处 `dma_buf_virt_` 重构 + init.cpp/test 接线 + host 全量编译 | ✅ | 2595eb5 | 705/0（重构，数不变） |
| 批4 | 收尾：ROADMAP/PLAN/todo/`document/notes` + 全量 run-kernel-test | ✅ | (本次) | 705/0 |

**完成总结**（694→705，M4 +11）：块设备抽象落地——`IBlockDevice`（最小同步接口，`ErrorOr<void>`，A.6）+ `RAMBlockDevice`（内存桩，Heap 配对 move-only）+ `AHCIBlockDevice`（薄包装 AHCI，复用 M3 `g_dma_pool` 的 `DmaBuffer`，不碰 ahci.cpp）+ Ext2 解耦（`AHCI&,port`→`IBlockDevice*`，淘汰 ad-hoc DMA `ensure_dma_buffer`，`dma_buf_virt_`→`block_buf_[4096]` 普通数组，净减 29 行）。架构：A.6 ErrorOr 接口；ext2.hpp 不再依赖 ahci（更解耦）。关键教训 GOTCHA #8（QEMU AHCI 容量边界）。`block_count()` identify + `flush()` 真命令 + ahci.cpp 内部 DmaPool 迁移留 F5-M1。**F1（内核基础设施）全部里程碑完成**。

## ✅ F5-M1（AHCI DMA 迁移）已完成 — 2026-06-16

> 目标：收编 ahci.cpp 内部 ad-hoc DMA（command list/FIS/command table 的手动 `g_pmm`+`g_vmm.map`+硬编码 `+0xFFFFFFFF80000000ULL`，[ahci.cpp:132-191](kernel/drivers/ahci/ahci.cpp#L132-L191)）+ execute_command 手动 PRDT（[ahci.cpp:256-265](kernel/drivers/ahci/ahci.cpp#L256-L265)）→ M3 `DmaPool`/`PrdtBuilder`，闭环 block device→AHCI DMA 栈；补 `AHCIBlockDevice` 的 `block_count()`（ATA IDENTIFY）+ `flush()`（FLUSH CACHE）M4 占位。
> 决策：批1 保持 setup_port DMA 布局不变（command list+tables 同 4KB DmaBuffer、FIS 单独），只换来源 → DmaPool（低风险）；identify 容量先 28-bit（words 60-61）；AHCI::read/write 保 bool（legacy 渐进），identify/flush 新方法走 ErrorOr。
> 不做：AHCI 中断驱动（仍轮询）、NCQ、VirtIO/NVMe（F5-M2/M3）。BAR5 MMIO 映射（[ahci.cpp:65](kernel/drivers/ahci/ahci.cpp#L65)）是设备寄存器非 DMA，保留。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | setup_port DMA 收编：command list+tables / FIS 手动 PMM+VMM+硬编码偏移 → `g_dma_pool`（DmaBuffer per-port 成员，替 `cmd_list_phys_`/`fis_buf_phys_`），布局不变。GOTCHA #7（release 只 free phys） | ✅ | b97bb1d | 705/0（重构，数不变） |
| 批2 | execute_command PRDT 手动 `prdt[0]` → `PrdtBuilder`（scatter-gather，单段也用，输出→`HBAPrdtEntry`） | ✅ | f583f5e | 705/0（重构，数不变） |
| 批3 | ATA IDENTIFY（`AHCI::identify`→容量，`block_count()` 真值）+ FLUSH CACHE（`AHCI::flush`，`flush()` 真命令） | ✅ | fdaea2a | 705/0（identify/flush 真机过） |
| 批4 | 收尾：ROADMAP/PLAN/todo/notes + 全量验证（FORTIFY 对等） | ✅ | (本次) | 705/0 |

**完成总结**（705→705，F5-M1 重构+新功能，测试数不变）：ahci.cpp 内部 ad-hoc DMA 全收编 M3 基建——`setup_port`（command list+tables/FIS → `g_dma_pool` DmaBuffer per-port，替手动 PMM+VMM+硬编码 `+0xFFFFFFFF80000000ULL`，删未用 MMIO 映射）+ `execute_command` PRDT（手动 `prdt[0]` → `PrdtBuilder` scatter-gather）+ ATA IDENTIFY/FLUSH CACHE（`execute_command`/`build_cfis` 参数化 `command` byte；`AHCI::identify` 解析 words 60-61 28-bit 容量 → `AHCIBlockDevice::block_count()` 真值；`AHCI::flush` → `AHCIBlockDevice::flush()` 真命令）。**闭环 block device→AHCI DMA 栈**：M3 DmaPool/PrdtBuilder 真正落地到 AHCI 驱动。验证 QEMU 真机 `block_count()>0`（IDENTIFY 过）+ flush 不崩 + FORTIFY 对等（本地复现 CI）。遗留：中断驱动（仍轮询，todo 目标 3）留后续。

## ✅ F2-M1（VMA 区域记账）已完成 — 2026-06-16（PR #7 `a65d8ff` squash）

> 目标：给 `AddressSpace` 补 VMA（Virtual Memory Area）区域记账——追踪每进程"哪段虚拟地址 / 什么权限 / 匿名还是文件映射"，为 mmap/munmap/brk/demand paging/CoW 提供单一事实源。M1 只做记账 + 让 PF handler 用它做合法性校验（无 VMA 命中 → 真 segfault），不做 mmap（→M2）/brk（→M3）。
> 决策（propose 已确认）：
> - **#1 范围含闭环**（批1-4，PF 校验是 VMA 真价值；否则账本成死数据到 M2 才用）。
> - **#2 `IVMAStore` 走 `ErrorOr`**（A.6，新代码无 legacy，非 todo 草案 bool）；`AddressSpace::map` 仍 bool（渐进迁移同 M4 批3）。
> - **#3 VMA 结构体预留 `backing_inode`/`file_offset`**（forward-decl `fs::Inode`），M1 只测匿名区域，文件映射填值留 M2。
> - **#4 批1 单测走 `kernel/test/`**（计数进 run-kernel-test，同 M3 惯例）。
> **实机冒烟（用户要求）**：批4 收尾 run-kernel-test 全绿后，`timeout` 拉起真内核（`make run`，GUI 启动 + 用户程序）确认不炸——防"kernel-test / host-test 三绿但一进真内核就炸"（PF/execve 改动高危）。非完成门（GUI 无断言），观察性保险。
> 依赖就绪：kernel heap（new VMA）/ Spinlock irq_guard（M1 RingBuffer 同款）/ ErrorOr（M0）。
> 不做：mmap/munmap/mprotect（M2）、brk（M3）、Page Cache（M4）、demand paging 增强（M5）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `vma.hpp/cpp`：`VmaFlags` + `VMA` 结构体 + `IVMAStore` 抽象 + `LinkedListVMAStore`（insert 有序+合并 / find / remove 拆分 / find_free_area）+ 单测 | ✅ | a65d8ff | 710/0（+5） |
| 批2 | `AddressSpace` 持 `LinkedListVMAStore`+`Spinlock` 成员（`vmas()`/`vma_lock()` 访问器，构造建/析构 RAII）+ `LinkedListVMAStore` 补 move（AddressSpace move-only 需成员可 move）+ `memory_layout` 加 `USER_BRK`/`MAP` 常量（按实际栈顶≈32GB 校正，非 todo 127TB） | ✅ | a65d8ff | 712/0（+2） |
| 批3 | execve ELF 段（PF_W/PF_X→VmaFlags）+ init/gui_init 用户栈（Stack flag）注册 VMA；insert 失败→路径失败保 VMA 完整 | ✅ | a65d8ff | 712/0（启动路径未被 run-kernel-test 执行，靠批4 实机） |
| 批4 | PF demand paging 加 VMA `find()` 诊断（未命中 klog_warn 但仍 demand page，不改行为；真 segfault 留 M5）+ 收尾 + 实机冒烟 | ✅ | a65d8ff | 712/0 + 实机启动不炸 |

**完成总结**（705→712，F2-M1 +7）：VMA 记账基础设施落地——`LinkedListVMAStore`（侵入式有序链表，insert 合并 / remove 拆分 / find / find_free_area，store-owns RAII）+ `IVMAStore` 抽象（可换红黑树）+ AddressSpace 集成（值成员 `vma_store_` + `Spinlock`，补 move）+ execve/栈注册（PF_W/PF_X→VmaFlags；Stack flag）+ PF demand paging VMA `find()` 诊断（未命中 warn 不改行为，真 segfault 留 M5）。架构：A.6 ErrorOr（逻辑错误）；A.7 不入 Cinux-Base（依赖 heap）；用户布局常量按实际栈顶≈32GB 校正（非 todo 草案 127TB）。关键教训：operator new 返 nullptr 非 panic（OOM 崩惯例）；klog_warn 是宏禁加命名空间前缀；启动路径不被 run-kernel-test 覆盖（靠实机冒烟）。遗留：PF 硬门控（M5）/ fork VMA 复制（F3）。

## ✅ F2-M2（mmap/munmap/mprotect）已完成 — 2026-06-17（PR #8 `d3b7cfa` squash）

> 目标：实现 mmap/munmap/mprotect syscall（Linux 9/11/10），消费 M1 VMA（`find_free_area`+`insert` / `remove` / flags），让用户程序动态内存映射。mmap 懒分配（仅建 VMA，PF 时 demand page，兼容 M1 批4 诊断）。
> 决策（propose 已确认）：
> - **范围调整**：T5 execve 注册（M1 批3 已做）/ T4 PF kill（M1 批4 推迟 M5）**不重做**；T6 fork VMA 复制放批4。
> - **匿名优先**（批1-3）；文件映射 `backing_inode` 批4 基础（真 Page Cache 留 M4）。
> - **syscall 返回 -errno**（A 翻译边界，`errno.hpp`）；`USER_MMAP_BASE/END`（M1 批2 [4GB,24GB)）mmap 用。
> **实机冒烟（批4）**：改 syscall 表 + fork VMA，启动路径，`timeout make run` 兜底。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `sys_mmap`（9）：匿名映射 + `find_free_area`/MAP_FIXED + VMA insert（懒分配）+ PROT/MAP 常量 + errno + 单测（set_current 模式） | ✅ | d3b7cfa | 716/0（+4） |
| 批2 | `sys_munmap`（11）：VMA `remove` 拆分 + 释放 demand-paged 物理页 + `unmap` + 单测 | ✅ | d3b7cfa | 719/0（+3） |
| 批3 | `sys_mprotect`（10）：VMA flags（保留 base 替换 R/W/X）+ PTE re-map + 单测 | ✅ | d3b7cfa | 721/0（+2） |
| 批4 | fork VMA 复制（T6，含 backing）+ 文件映射基础（fd→Inode backing，内容 M4）+ vma.hpp backing 修正 InodeOps*→Inode* + 收尾 + 实机冒烟 | ✅ | d3b7cfa | 721/0 + 实机不炸 |

**完成总结**（712→721，F2-M2 +9）：mmap 三 syscall 落地——`sys_mmap`（9，匿名/文件映射，懒分配 + `find_free_area`/MAP_FIXED + VMA insert）+ `sys_munmap`（11，VMA remove 拆分 + 释放 demand-paged 页 + unmap）+ `sys_mprotect`（10，保留 base 替换 R/W/X + PTE re-map）+ fork VMA 复制（CoW 页表后克隆父 VMA 含 backing）+ 文件映射基础（fd→Inode backing，内容 demand-read 留 M4）。架构：A 翻译边界（ErrorOr→errno，`errno.hpp`）；syscall handler 统一 6 参（SyscallFn）；VmaFlags 补 `operator&`（mprotect 提取 base）。关键校正：批1 VMA backing 用 `InodeOps*` 错（inode.hpp 是 `struct Inode` + `InodeOps` vtable），批4 改 `Inode*`。实机冒烟启动到 GUI 不炸。遗留：文件映射 demand-read 内容（M4 Page Cache）/ PF 真 segfault（M5）。

## ✅ F2-M3（brk）已完成 — 2026-06-17（PR #9 `4331853` squash）

> 目标：`sys_brk`（Linux 12）用户态堆（malloc 底层）。**懒 brk**：调 `brk_current`（边界检查），不 map/unmap 页，堆区访问 PF 时 demand page（和 M2 mmap 懒分配一致，复用 M1 批4 诊断）。
> 决策：懒 brk（非 todo eager map，与 M2 统一）；Task 加 `brk_current`/`brk_initial`/`brk_max`；execve 设 `brk_initial`（ELF 段末尾）+ Heap VMA `[brk_initial, USER_BRK_MAX)`。
> 不做：`user/test_brk.c` + sbrk libc wrapper（留后续）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | Task brk 字段 + `sys_brk`（12，懒：addr==0 返当前 / 越界返当前 / 否则调 brk_current）+ 注册 + 单测 | ✅ | 4331853 | 722/0（+1） |
| 批2 | execve 设 `brk_initial`（ELF 段末尾）+ Heap VMA `[brk_initial, USER_BRK_MAX)` + 收尾 + 实机冒烟 | ✅ | 4331853 | 722/0 + 实机不炸 |

**完成总结**（721→722，F2-M3 +1）：brk 落地——`sys_brk`（12，懒：调 `brk_current`，边界 `[brk_initial, brk_max]`，不 map/unmap，PF demand page）+ Task `brk_current`/`brk_initial`/`brk_max` 字段 + execve 设 `brk_initial`（ELF 段末尾页对齐）+ Heap VMA `[brk_initial, USER_BRK_MAX)`。架构：**懒 brk**（与 mmap 统一，复用 demand paging）；syscall handler 6 参；brk 不返 errno（返地址，Linux 语义）。实机冒烟启动到 GUI 不炸。遗留：`user/test_brk.c` + sbrk libc wrapper（用户程序实际用 brk 时加）。

## ✅ F2-M4（Page Cache）已完成 — 2026-06-17

> 目标：内核级 Page Cache，让 **file-backed mmap 的 demand paging 读到真文件内容**（M2 批4 留的洞：[sys_mmap.cpp:128-134](kernel/syscall/sys_mmap.cpp#L128-L134) 只记 `backing` inode，PF 时 [exception_handlers.cpp:269](kernel/arch/x86_64/exception_handlers.cpp#L269) 一律映射零页 → 文件映射读到全零）。缓存键 = `(Inode*, page_offset)`。
> 决策（propose 已确认）：
> - **#1 最小 MVP**：cache + file-mmap demand-read（读路径）。脏页写回 / MAP_SHARED 写一致性、全 `read()` 经缓存、LRU+跨进程共享+CoW **留后续**。
> - **#2 从干净 main 开**（M4⊥brk(M3)；侦察期间 PR #9 M3-brk 已合入 main，M4 分支天然含 M3）。
> - **#3 复用 direct-map**：缓存页 virt = `phys + KERNEL_VMA`（免 temp-map、不 unmap，GOTCHA #7 同 M3 DmaPool）。
> - **#4 读在锁外、insert 在锁内**：PF handler 跑 IF=0，`get_page` 先把文件内容读到已 present 的 direct-map 页（锁外，AHCI 轮询 IF=0 成立），再短临界区 insert（irq-guard Spinlock），杜绝 IO-under-lock 重入死锁 → 新 GOTCHA。
> - **PTE 权限按 VmaFlags 翻译**（Write→WRITABLE、!Exec→NX；现在硬写 WRITABLE，批2 修正）。
> 依赖就绪：M1 VMA（`backing`/`file_offset`/`VmaFlags`）/ M2 mmap 文件 backing / ErrorOr / PMM `alloc_page_locked` / VMM `map_nolock` / `Ext2FileOps::read(inode,offset,buf,count)`。批1 待核对 `Inode` 经 `ops->read` 暴露 read。
> 不做：脏页写回（MAP_SHARED 写一致性）/ 全 `read()` 经缓存（→M6 ext2 Cache）/ LRU 淘汰 / 跨进程共享缓存 + CoW-for-shared-file（→F3/M5）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `page_cache.hpp/cpp`（`CachedPage`+`PageCache` 256-bucket hash，`lookup`/`get_page` 填充+EOF 零填/`release`/stats，`ErrorOr`，irq-guard Spinlock，direct-map virt）+ fake `InodeOps` mock + `test_page_cache.cpp` 单测（命中/未命中填充/refcount/同 inode+offset 二次命中/EOF 零填） | ✅ | db42957 | 728/0（+6） |
| 批2 | `handle_pf` 文件感知（file-backed VMA→`get_page`→`map_nolock`；PTE Write→WRITABLE，NX 留 F9 NXE；读锁外 insert 锁内）+ `sys_mmap` offset 页对齐校验 + mprotect 移除 NX（NXE 未启用保留位）+ 匿名路径不变 | ✅ | db42957 | 728/0（回归；文件路径单测 dormant，批3 Test B 锻炼） |
| 批3 | 真文件 mmap 闭环（`test_file_mmap.cpp`：Test A `get_page` 真盘读 ext2 字节比对 + cache hit；Test B 文件 VMA `as.activate()` 后访存经 #PF→handle_pf 文件路径→字节比对，端到端验证 wiring）+ NX 修复 + 收尾 | ✅ | db42957 | 730/0（+2） |

**完成总结**（722→730，F2-M4 +8）：Page Cache 落地——`CachedPage` + `PageCache`（256-bucket hash，`(Inode*, page_offset)` 键，direct-map virt = `phys + KERNEL_VMA`，ref_count，无淘汰）+ `get_page`（命中 bump ref / 未命中 alloc 页 → 锁外 `inode->ops->read` 填充 + EOF 零填 → 锁内 insert，IF=0 安全）+ `lookup`/`release`/stats + `handle_pf` 文件感知（file-backed VMA → `get_page` → `map_nolock`，PTE Write→WRITABLE，NX 留 F9 NXE；匿名路径不变；execve ELF 段是匿名故 boot 走匿名）+ `sys_mmap` offset 页对齐校验。验证：批3 `test_file_mmap` Test A（cache 真盘读 ext2 字节比对 + cache hit）+ Test B（文件 VMA `as.activate()` 后访存经 #PF→handle_pf 文件路径→字节比对，端到端验证 wiring）。架构：A.6 ErrorOr（`get_page`→`ErrorOr<CachedPage*>`）；A.7 不入 Cinux-Base（依赖 heap/PMM/Inode）；复用 direct-map（GOTCHA #7 同 DmaPool，不 temp-map 不 unmap）。关键教训 GOTCHA #10（NXE 未启用→NX 保留位，Test B PF round-trip 定位）。MVP 只做读路径：脏页写回 / MAP_SHARED 写一致性 / 全 `read()` 经缓存 / LRU+跨进程共享+CoW 留后续（M6/F3/M5）。

## ✅ F2-M5（Demand Paging 硬门控）已完成 — 2026-06-17

> 目标：把 PF handler「VMA 未命中→映射零页容错」（[exception_handlers.cpp:258-271](kernel/arch/x86_64/exception_handlers.cpp#L258-L271)）升级为 **VMA 硬门控**——用户态 not-present PF 无 VMA 命中 → 真 segfault（终止进程），兑现 M1 记账价值。命中合法 VMA → 照常 demand page（匿名零页 / 文件 page cache，M4 路径不变）。
> 决策（propose 已确认，2026-06-17）：
> - **#1 segfault 终止**：批1 两种方式都 spike（直接 `exit_current()` vs 标记 Dead+延迟退出）再定，按 IF=0 中断栈 task switch 语义选稳的。
> - **#2 栈增长**：Stack VMA 扩到 ~1MB 向下 demand-page 自动增长 + 栈底 guard page（溢出→segfault），现有程序不崩。
> - **#3 权限门控范围**：只做「无 VMA→segfault」；写只读/执行权限违规留后续（NX 因 NXE 未启用留 F9，GOTCHA #10）。
> 终止机制复用 [scheduler.cpp:179-200](kernel/proc/scheduler.cpp#L179-L200) `Scheduler::exit_current()`（F3 信号未做，临时等价 SIGSEGV-killed）。
> 依赖就绪：M1 VMA find / M2 mmap / M3 brk / M4 page cache / fork CoW（present 分支不受 not-present 门控影响）。
> 不做：SIGSEGV 信号交付（F3）/ NX 强制（F9）/ 栈 ulimit / page cache 跨进程 CoW（F3）/ mmap 脏页写回（后续）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | spike segfault 终止（`exit_current` 在 PF handler IF=0 中断栈可行性）+ handle_pf not-present user 硬门控（无 VMA→segfault；命中→demand 不变）+ 单测 | ✅ | 02e22c2 | 730/0（回归） |
| 批2 | Stack VMA 扩 ~1MB 自动增长 + 栈底 guard page（溢出→segfault） | ✅ | 652f698 | 730/0（回归） |
| 批3 | 全路径回归（execve ELF/brk/mmap/fork CoW 硬门控下合法放行）+ 修漏注册 VMA + 实机冒烟迭代 | ✅ | —（纯验证，无代码改动） | 730/0 + GUI 实机不炸 |
| 批4 | 收尾 + 全量 run-kernel-test + 实机冒烟（init/gui/shell 不崩=成败判据）+ ROADMAP/PLAN/todo/notes + GOTCHA（PF-exit 约束/栈增长/NXE 留 F9） | ✅ | (本次) | 730/0 + 实机不炸 |

**完成总结**（730→730，F2-M5 门控+栈增长，测试数不变）：demand paging 硬门控落地——`handle_pf` not-present user PF，`vma==nullptr` 时真实 user-mode fault（`err&0x04`）→ `klog_error` + `Scheduler::exit_current()` 终止进程（SIGSEGV 等价，F3 信号前临时方案；`context_switch` 抛弃 prev 中断帧切 next，不返回此处）；kernel-mode 访问用户地址（ring0 test / `copy_to_from_user`，`!(err&0x04)`）保持零页容错（不误杀 kernel-test PF 注入）。命中合法 VMA → demand 不变（匿名零页 / M4 文件 page cache）。栈增长：`USER_STACK_GROWTH=1MB` 常量（[usermode.hpp](kernel/arch/x86_64/usermode.hpp)），init.cpp/gui_init.cpp Stack VMA 扩到 `[TOP-1MB, TOP)`（仅顶 16KB 预分配，余 demand 增长），VMA 底外→segfault（guard）。架构：user-mode 判定靠 `err&0x04`（test 豁免天然分界，无需新 ErrorOr，复用 `exit_current`）。关键教训 GOTCHA #11（PF 硬门控 user-mode 判定 + 栈增长配套 + execve 复用 AS 不清 VMA）。验证：730/0 回归（kernel-mode 路径）+ `make run` GUI 启动到桌面不崩（user-mode 路径，无 segfault/panic）+ 全路径代码分析（所有合法 demand PF 有 VMA 覆盖）。遗留：SIGSEGV 信号（F3）/ NX 强制（F9）/ 权限违规门控（写只读）/ segfault 进程资源清理（`exit_current` leak，待 task exit cleanup）。

## OPEN GOTCHAS（跨里程碑通用，活警告）
1. **验证 target**：内核改动用 run-kernel-test（~694 项）；host 单测（`test/unit/`）不在其中，改被 mock 类后 push 前补全量编译（L5）。
2. **Cinux-Base 是子模块**：`Logger`/`LogLevel`/`RingBuffer` 在 `third_party/Cinux-Base/include/cinux/*.hpp`，复用勿重写。
3. **里程碑区分「类型就绪」vs「内核消费」**（M0/M1/M2 同构）：Cinux-Base 类型常先就绪，待办是 kernel/ 增量消费。
4. **klog_* 实时输出的 reentrancy guard**（批4a）：`g_klog_emit_depth` 让 klog sink 跳过 log 自身的 kprintf，避免同一行双重入队——是 kprintf→klog_* 迁移的前提（否则迁后丢 console）。定义须在 `log()` 前。
5. **崩溃 ring 读不到**：fatal_halt/kpanic 死循环，ring 历史崩溃后读不到；非崩溃异常(#DB/#BP/#GP-user)/error 进 ring 可 dmesg 读。kpanic/dump 保留 kprintf（实时诊断/halt）。
6. **kprintf 全量迁移未做**：294 个（除 mini 148）留渐进；mini 148 不迁（早期启动无 ring）。vkprintf_impl 第三参是 va_list 非 va_args，需 va_list 或手动 format。
7. **direct-map（phys+KERNEL_VMA）勿 unmap**（批2 教训，2026-06）：direct-map 是 phys↔virt 永久固定映射 + demand-paged，整个 kernel 共用。DMA/映射代码 free 时只回收 phys（`free_pages`），**绝不 `vmm.unmap(phys+KERNEL_VMA)`**——拆槽会让后续 demand paging 反复映射错 phys 死循环（QEMU 卡死，DmaPool 首版教训）。AHCI 同款（map 不 unmap）。free_page_count 因页表 demand-page 开销不精确对称，测试断方向不断绝对。
8. **QEMU AHCI 容量 ≠ ext2.img 文件大小**（M4 批2 教训，2026-06）：写高号 sector（如 7000，文件层面 8192 sector 内）仍 `[AHCI] command timeout`——QEMU AHCI IDENTIFY 报告的容量几何 < 文件大小，越界写不响应。真机写测试用已知可写的低 sector（`test_ahci_write` 的 sector 2000 = ext2 块 1000）+ 读原值/写回 restore，避免破坏 ext2（后续 ext2 套件同次运行依赖干净盘）。`AHCIBlockDevice::block_count()` 精确值待 F5-M1 ATA IDENTIFY。
9. **kernel freestanding 内存操作用 `kernel/lib/string.hpp`，禁 `<cstring>`/`<string.h>`**（M4 CI 教训，2026-06）：CI（Ubuntu glibc + 编译器 spec 默认 `_FORTIFY_SOURCE=2`）把 `<cstring>`/`<string.h>` 的 `memcpy`/`memset` 宏改写为 `__memcpy_chk`，但 kernel freestanding 不链 libc → `big_kernel_test`/host 链接 `undefined reference to __memcpy_chk`（PR #5 kernel-tests+host-tests Build 双炸）。本地无 FORTIFY spec 故未复现（GOTCHA #1 同类盲区：本地绿≠CI 绿）。一律用 `kernel/lib/string.hpp` 的 kernel 实现（非 fortify）；`memcmp` 不被 fortify 改写但为一致同源。
10. **EFER.NXE 未启用 → NX bit（bit63）是保留位**（F2-M4 批3 教训，2026-06）：F9 NX/SMEP/SMAP 未做，NXE 关闭。PTE 设 `FLAG_NX`（bit63）后访问该页触发 **reserved-bit #PF（err=0x8）循环**——`handle_pf` 文件路径初版给非 exec 页设 NX，致 Test B PF round-trip 无限循环。诊断：PF handler 临时打印 err/cr3/asPml4/translate，err=0x8(RSVD) + translate==phys 确认 PTE 设对、是保留位违例。修复：`handle_pf` 文件路径 + `sys_mprotect` 都**暂不设 NX**（非 exec 页不强 NX），留 F9 启用 NXE 后再开。另：单测验 `handle_pf` 文件 PF 需 `as.activate()` 切到进程 PML4 再访存（boot PML4 user 半空）；`get_page` 读文件在 cache 锁外、insert 在锁内（IF=0 防 IO-under-lock 重入死锁）；缓存页 virt 复用 direct-map（GOTCHA #7 同 DmaPool）。
11. **PF handler 硬门控的 user-mode 判定 + 栈增长配套**（F2-M5 教训，2026-06）：`handle_pf` 杀进程（`exit_current`）必须判 `err & 0x04`（真实 user-mode fault）——kernel-test 是 ring0 访问用户地址（`err&0x04=0`），误杀会让测试 hang（test_file_mmap Test B 模式：`CurrentTaskGuard` 设 tmp task 但 ring0 访存）。`exit_current` 的 `context_switch` 抛弃 prev 中断帧切 next，不返回 PF handler（segfault 终止可行；"标记 Dead+延迟退出"反而要伪造中断帧，更复杂）。**栈 VMA 必须与硬门控配套**：门控上了但栈仍固定 16KB → 深调用栈用户程序栈 PF 落 VMA 外 → segfault（run-kernel-test 不跑真实用户深栈故绿，掩盖此依赖；init.cpp/gui_init.cpp 用 `USER_STACK_GROWTH=1MB` 扩 Stack VMA）。**execve `clear_user_mappings` 只清页表不清 VMA store**——Stack VMA 经 fork 继承 / execve 复用 AS 保留传播到所有用户程序。run-kernel-test 全 kernel-mode fault，不覆盖 user-mode segfault/demand 路径（靠 `make run` 实机 + `test_shell_*` 间接）。
12. **read() 经 PageCache 的递归规避 + pipe 判别**（F2-M6 教训，2026-06）：`read_bytes` 是**新函数**（sys_read 对 `is_page_cacheable()` 真者调它），其内部 `get_page` 填充走 `inode->ops->read`（Ext2FileOps 读盘原语）——`Ext2FileOps::read` **不改不调 page_cache**，否则 read→get_page→read 死循环。判别"磁盘文件 vs pipe"不能用 `inode->type`（pipe 的 type 也是 `Regular`，[test_sys_pipe.cpp:105]），禁 RTTI 无法 dynamic_cast；用 `InodeOps::is_page_cacheable()` virtual（默认 false，Ext2FileOps override true）。`g_page_cache.hit_count()` 是 boot 全局跨测试累积，断言"二读命中"须在二读前捕获基线。
13. **direct-map 独立窗口 + KERNEL_VMA 重载区分**（F2-M7 direct-map 前置教训，2026-06）：`phys_to_virt` 用 `DIRECT_MAP_BASE=0xFFFF880000000000`（PML4[272]，loader 1GB/2MB 大页 identity 映全 RAM），**不是** KERNEL_VMA。KERNEL_VMA 窗口硬限 2GB 且 boot 只映 higher-half 0-1GB，phys>1GB 落未映射处 demand-page 非 identity（latent bug，buddy 侵入式链表写 high phys 触发 PF 重入踩烂 → 死循环）。`-cpu max` 仅 KVM 时设（qemu.cmake），无 KVM 落回 qemu64 不暴露 PDPE1GB → direct_map_up_to 必有 2MB 页 fallback。**迁移严判**：访问任意 PMM 页（页表/DMA/缓存页/GS）是 direct-map→DIRECT_MAP_BASE；kernel image 相对（pmm bitmap `__kernel_stack_top - KERNEL_VMA`、kernel 链接基址、boot higher-half PT）保留 KERNEL_VMA。direct-map PT 页放 [0x10000,0x20000)（<1MB 不过 PMM，持久）。**direct-map 区域（PML4[272]，`DIRECT_MAP_BASE+…`）严禁 `VMM.map/unmap/translate`**：direct-map 是 loader 建的 1GB/2MB huge 永久 identity 映射，全栈共享。`VMM::map` 内 `walk_level` 遇 huge entry（PS bit）会 **split**（拆成 4KB PT + 改写原 entry）；对 direct-map 的 1GB huge 触发即破坏全局 direct-map → 后续 `phys_to_virt` walk 错乱命中 phys 0 BIOS 数据当 PT → reserved-bit PF（err=0x9）。教训：M3 `DmaPool::alloc` 旧逻辑对 virt 调 `g_vmm.map`（M3 时 virt 在 KERNEL_VMA 窗口，map 无害），批2 迁 direct-map 后漏删 map → 首次 AHCI `g_dma_pool.alloc` 即崩（`test_cache_reads_real_file`，fresh build 才暴露）。修法：direct-map 复用 loader 永久映射——alloc 只取 phys（virt=phys+DIRECT_MAP_BASE 直接可用，**不 map**），free 只 free phys（**不 unmap**）。`VMM.translate` 对 huge 也不支持（walk_level huge+!alloc 返 nullptr），故 direct-map virt 不该经 translate 验证。
14. **WSL2 nested KVM（AMD）EPT 对侵入式 free-list 写读不一致**（F2-M7 Bug2 教训，2026-06-18）：buddy 初版侵入式 free-list 把 `next` 指针写在 free 页头（经 direct-map `phys+DIRECT_MAP_BASE`），依赖 direct-map 写读严格一致。WSL2 nested KVM（AMD `-accel kvm -cpu max`，hypervisor flag=14）EPT 对「huge page 内 sub-page 写」做不到一致——同地址 `0xFFFF880040000000`（phys 1GB）main 单次读返 valid（260096）、buddy op 读返 poison（`0xCAFEBABEDEADC0DE`），振荡→`pop_free` 遍历 #GP（`test_wm_close_button_closes_terminal_pipes`）。**TCG（`CINUX_NO_KVM=1`，2MB path）742/0 全绿**证明 buddy wiring 本身正确（根因 nested KVM 物理层，非逻辑 bug）。**修 = buddy 改非侵入式 per-order bitmap free-list**（bitmap 存 metadata 区，不写 free 页，KVM nested safe；`find_first_set` 天然 low-first）。诊断教训：CHK（main 单次读）valid ↔ buddy op 读 poison 的**同地址振荡**→ nested KVM EPT 嫌疑；**TCG 对比（`CINUX_NO_KVM` env）是定位物理层 bug 的利器**（逻辑层错 TCG 也复现，物理层错 TCG 绿）。凡依赖 direct-map sub-page 写读一致的侵入式结构（free-list/对象头）在 nested KVM 都有此风险，优先用 metadata 数组。
15. **slab 复用暴露按指针键控的缓存（F2-M7b 批2 教训，2026-06-18）**：page cache 原按 `Inode*` 指针做 hash/lookup 键。slab（正确）复用已释放 Inode 的内存地址给新 Inode → 新文件查 cache 时**命中陈旧页**（旧文件内容），`sys_read` 返回错字节（`test_shell_write` echo-redirect 读回 `"Hello from e"` 而非 `"hello world\n"`；Heap first-fit 侥幸不复用同地址故潜伏）。**根因非 slab**（复用是 slab 本职），是 page cache 用可复用指针当稳定键。修：cache 改按 `inode->ino`（稳定号）键控（hash/lookup/insert）。**通用铁律**：任何按对象指针/地址键控的在线结构（cache/table），当对象经 slab/heap 分配释放时都有同款陈旧命中风险，键须用稳定标识（id/number）而非指针。
16. **sigreturn 栈注入 trampoline 依赖 NXE 关闭 + Custom 走中断路径（F3-M1 教训，2026-06-18）**：Custom handler 的 sigreturn trampoline 是栈上 `int $0x80`（cd 80）代码，handler 返回地址指向它 —— 依赖栈页可执行。NXE 未启用（GOTCHA#10）故可行。**F9 启用 NXE 后栈不可执行，trampoline 失效，须迁 vdso/独立可执行页**。另：当时 `syscall.S` 只保存精简帧，sigreturn 经 syscall 无法完整恢复用户上下文 —— 故 Custom signal 投递挂**中断/异常返回路径**（ISR 宏 `call handler` 后 `signal_check_deliver_isr`），sigreturn 经 **IDT vector 0x80 trap gate（DPL=3）** 收完整 InterruptFrame 恢复；syscall 路径（`signal_check_and_deliver`）只投递 Default/Ignore，Custom 留 pending 给下次 IRQ0（时钟，延迟可忽略）。`signal_check_deliver_isr` 严判 `frame->cs & 0x03`（只用户态投递）—— kernel-test 全 ring0 中断点 skip，保护测试设的 pending 栈 task 不被误投递（exit_current 切走栈 task 崩）。2026-06-27 后 syscall frame 已扩到 128B 并保存 R12-R15+RBX+RBP，但 Custom signal 仍沿中断完整帧路径。
17. **wrmsr FS_BASE/GS_BASE 须规范地址，否则 #GP err=0（F3-M2 批1 TLS 教训，2026-06-18）**：x86-64 要求 FS_BASE/GS_BASE 段基址 MSR（`0xC0000100`/`0xC0000101`）持有**规范地址**（bits 48..63 须符号扩展 bit 47：低半 `0x0000...` / 高半 `0xFFFF...`）。写**非规范**值（如测试的 `0x1234567890ABCDEF`，bit 47=0 但 bit 48..63 非全 0）→ wrmsr 立刻 `#GP` err=0。诊断关键：panic 现场 RAX/RDX/RCX 全是 wrmsr 操作数（未进下条指令）→ 故障钉死在 wrmsr 本身 → 排除操作数错误即「MSR 不接受该值」→ 规范地址约束。真实 TLS 基址是用户指针（天然规范），clone(CLONE_SETTLS)/arch_prctl 不受影响——**主要坑测试与未来任何手写 FS/GS base 的代码**。`tls.hpp` 文档已注明。**此坑暴露 timeout 40 的价值**（panic 死循环挂死终端，timeout 杀进程暴露现场才定位，→ DIRECTIVES L5 补 timeout 40）。
18. **clone 子进程用户栈返回 = patch 帧 user_rsp 槽（F3-M2 批4 教训，2026-06-18）**：clone 子进程要以调用者 `stack` 参数返回用户态。syscall_entry 从 `%gs:0`(=kernel_stack_top) 载核栈，pt_regs 帧固定在栈顶，**user_rsp 在帧 offset 0**。clone 复用 fork 的拷核栈机制，拷完后直接 `*(uint64_t*)(child->kernel_stack_top-kSyscallFrameSize) = stack;` patch——**无需从 current_rsp 算偏移**（帧在栈顶固定位置）。子进程经 fork_child_trampoline(rax=0) 解卷回 syscall_entry → SYSRET（user_rsp=stack, rax=0）。2026-06-27 后 frame size 为 128B。详见 `document/notes/2026-06-18-f3-m2-clone.md`。
19. **改公共接口后，依赖它的测试断言由过变挂 → TEST_ASSERT 早 return 跳过清理 → 悬垂状态污染后续测试（F3-M2 批4 教训，2026-06-18）**：`TEST_ASSERT` 失败时 `return`（早返回）。若测试末尾有清理（如 `set_current(prev)` 还原 current），断言失败会跳过它 → `Scheduler::current_` 悬垂指向已销毁的栈 `Task tmp` → 后续测试读悬垂 current 崩。批4 改 `sys_getpid` 返 `tgid`（线程共享进程身份）使既有 `test_sys_getpid` 断言失败（设 `tmp.pid` 未设 `tmp.tgid`）→ 早返回 → current_ 悬垂 → 远处的 vfs `Spinlock::acquire` 崩（current_→垃圾 task→fd_table 垃圾），**表象远离真因，极具迷惑性**（像内存踩踏，实为悬垂指针）。诊断：二分（git stash 隔离批）+ 加 kprintf 打 current/task 地址。**通用铁律**：改公共接口（返回值/字段语义）后，grep 所有用到该值的测试断言同步改；测试用 `Task tmp` + `set_current` 的范式，断言失败早返回会留悬垂 current——危险，清理应放断言之前或用 RAII。
20. **fork/clone 栈拷贝 full_used 须 < 栈大小，否则下溢踩邻接（F3-M2 批4 教训，2026-06-18）**：`full_used = parent->kernel_stack_top - current_rsp`，子栈 `stack_size`=16KB。若 `full_used > stack_size`（测试用 `kernel_stack_top=rsp+16384` 即触发），`child_stack_start = child_stack_virt + stack_size - full_used` 下溢到栈映射前 → memcpy 踩邻接内存（latent，堆布局变即命中要害）。fork/clone 加 `if (full_stack_used > stack_size) full_stack_used = stack_size;` 上限防御（生产永不触发，栈远未满）。测试 `kernel_stack_top` 用小偏移（rsp+4096）让 full_used < 栈大小。
21. **单测设 current 用 Scheduler::set_current，勿直接 g_per_cpu.current（F3-M3 批3 教训，2026-06-19）**：`Scheduler::current()`（scheduler.cpp:234）返静态 `current_`，**不是** PerCpu 的 `g_per_cpu.current`。两者只由 `Scheduler::set_current(task)`（scheduler.cpp:238）同步设置（current_ + g_per_cpu.current 都设）。单测装栈 task 作 current **必须用 `Scheduler::set_current(&t)`**，cleanup 用 `Scheduler::set_current(nullptr)`——只设 `g_per_cpu.current` 会让 `current_` 仍 nullptr，任何经 `Scheduler::current()` 的 handler（sys_setpgid/setsid/未来 waitpid 阻塞等）拿到 nullptr → ESRCH，测试 fail（F3-M3 批3 首验 825/2 fail 定位此）。test_clone 的 futex 侥幸（futex 内部不经 Scheduler::current()），掩盖了这层。**通用铁律**：单测设 current 一律用 Scheduler::set_current（两者都设），勿直接写 g_per_cpu.current。
22. **TaskBuilder().build() 消耗全局 tid 计数器,跨测试文件污染（F3-M4 批4 教训，2026-06-19）**：`next_tid` 是全局单例,`TaskBuilder().build()` 每次分配递增。测试文件**共享**一个计数器,执行序固定（`run_signal_tests()` 在 `run_scheduler_tests()` 前）。新测试用 TaskBuilder 建 victim 分到 tid 1/2/3 → 后跑的 `test_build_basic_task` 断言首任务 `tid==1`（test_scheduler.cpp:62）失败（实际 4+）。**根因非逻辑**,是共享全局态 + 脆弱断言。**修**：纯状态机测试（只碰 state/sched_class/sig_actions/sig_pending）用**栈 `Task t{}`**（同 test_sig_state 范式）,零 tid/slab/核栈消耗 → 不污染计数器。**通用铁律**：跨测试文件共享全局计数器（tid/pid/next_*）,新测试用 TaskBuilder 建任务会位移他测的「首任务 tid==N」断言;能不分配就不分配（栈 Task 优先）。
23. **context_switch 恢复点的 current 必须读 per-CPU,不能用局部 next(F4-M4 M4-1 教训,2026-06-20)**：`schedule()`/`exit_current()` 里 `context_switch(&prev->ctx,&next->ctx)` **返回时跑在 next 的上下文,但执行的是 prev 当初被切出时**那条 `fxrstor`(prev 的栈帧)。该栈帧里的局部 `next` 还是 prev 当年的 next(已过期),**不能用 `next->fpu_state`**(会恢复错任务的 FPU)。旧代码用全局静态 `current_`(被「切到本任务那次」schedule 设成本任务)读对;current_→percpu 改造后须用 `current()`(读 `percpu()->current`,语义等价旧全局)。**通用铁律**：context_switch 返回后任何对「现在跑谁」的引用,读 per-CPU current(`current()`/`percpu()->current`),勿用 switch 时的局部 next/prev(跨切出-切入已过期)。同批另一教训:`Scheduler::init()` 须清 runq(`RoundRobin::clear()`)——测试间 stale task 会泄漏到下一个测试,首个真跑 `block(current)→schedule→pick_next` 的测试会选中 stale task 去真跑它本不该跑的 entry → 崩;boot 时 runq 空 no-op,生产无影响。
24. **prepare-to-wait 的 schedule_blocked() 必须在等待锁 irq_guard 析构后调用（F4-M4 M4-3 教训,2026-06-20）**:prepare_to_wait 提前设 state=Blocked,若 schedule 在持锁时跑(irq_guard 还活着)→持锁切走→lockdep panic 或死锁(切走的任务无法释放锁)。用嵌套 `{}` 块限定 guard 作用域:`{ auto g=spin_.irq_guard(); enqueue_waiter; prepare_to_wait(self); }` 先析构(release+sti),**再** `schedule_blocked()`。**通用铁律**:任何「先标记将睡、后 schedule」的 prepare-to-wait 模式,标记须持锁(irq_guard 关中断防本核 tick 抢跑),schedule 必须释锁后;否则持锁切走。配合 unblock 幂等(仅 state==Blocked 才入队,防 double-enqueue)关闭 lost-wakeup 窗口。详见 `document/notes/2026-06-20-f4-m4-3-prepare-to-wait.md`。
25. **✅[已闭环,F4-M4 M4-2-3]** **user task 迁移到 AP 确定性 GP(F4-M4 M4-2-2 教训,2026-06-20)**:AP1 完整 schedule 迁移 user task → 确定性 #GP。M4-2-2 现场判断「%gs 损坏(GS_BASE 非规范 0xF000FF53F000FF53)」**被 panic 递归污染误导,实为下游产物**。M4-2-3 用 `%gs` 安全首错捕获(`handle_gp` 顶部 `io_outb(0xE9,…)` 裸打 frame 字段 + `rdmsr(GS_BASE)` + `cr3`,不碰 %gs)抓到真正首错:`rip`=`schedule()` `prev->state` 解引用,`rax`(prev=`current()`)`0xf000ff53f000ff53`,`cr3`=内核 PML4,**`gs_base=0`**(规范,非损坏)。根因链:该 CPU 在 schedule 时 **GS_BASE=0** → `percpu()->current=%gs:24` 读地址 24(低内存 BIOS 身份映射)→ BIOS 远指针 `0xf000:0xff53` → 非规范 → GP(err=0:**x86-64 非规范地址访问产生 #GP 而非 #PF**)。GS_BASE 为何 0:`GDT::load()`(gdt.cpp)用平坦段重载 `%gs` 选择子,长模式下清零 `MSR_GS_BASE`;ap_main 先 `write_msr(GS_BASE,percpu)` 后 `gdt_blocks[cpu].init()`→`load()`,顺序致命(BSP 因 boot 时 GDT 在 usermode_init 设 GS 之前加载,无事)。**修:移除 `GDT::load()` 的 `%fs`/`%gs` 重载**(见 #26)。**通用铁律**:① 非 canonical 地址访问在 x86-64 是 #GP(err=0) 非 #PF;② GP handler 递归(current() 读 %gs)污染第一现场,`%gs` 安全首错捕获(outb/rdmsr/栈 frame,不碰 %gs)是诊断这类崩溃的正解,胜过 GDB;③ 「swapgs 全链代码正确」不排除别处清零 GS_BASE。详见 `document/notes/2026-06-20-f4-m4-2-3-migration.md`。

26. **长模式禁止 `mov $sel,%fs/%gs` 重载,会清零 MSR 基址(F4-M4 M4-2-3 教训,2026-06-20)**:`%fs`/`%gs` 基址在长模式由 `MSR_FS_BASE`/`MSR_GS_BASE` 管(per-thread TLS / per-CPU PerCpu),**不在 GDT 描述符**。`mov $sel,%gs` 会用 GDT 描述符基址覆盖 MSR——平坦数据段(基址 0)直接清零锚定。`lgdt` 后重载 `%ds/%es/%ss`(新 GDT 描述符可能不同)是对的,但 `%fs`/`%gs` 不应碰(null 选择子 + MSR 基址是长模式 percpu 标准做法,Linux 同款)。`GDT::load()` 旧版重载 %fs/%gs 是 #25 迁移 GP 的根因,已移除。**通用铁律**:任何 GDT/段初始化代码,加载后只 flush `%ds/%es/%ss`(+ `%cs` 经 far jump/ret),勿动 `%fs`/`%gs`;设其基址一律 `wrmsr`。

## ✅ direct-map 独立窗口（F2-M7 前置）Bug1 已修 — 2026-06-17（fresh build 734/0）

> 目标：修 `phys_to_virt` 的 latent >1GB direct-map bug（KERNEL_VMA 窗口只 1GB），为 buddy 接 PMM 铺路（buddy 侵入式链表写 high phys 需 identity direct-map）。
> 决策：独立窗口 `DIRECT_MAP_BASE=0xFFFF8800…`（PML4[272]，512GB）+ loader `direct_map_up_to`（1GB/2MB 大页 identity，不依赖 PDPE1GB）+ centralize `phys_to_virt`（phys_virt.hpp）+ 迁移 direct-map 站点（保留 kernel-image 站点 KERNEL_VMA）。详见 `document/notes/2026-06-17-direct-map-window.md`。
> **Bug1（批2 fresh build 暴露的 reserved-bits PF）已修（批3）**：根因 = `DmaPool::alloc` 对 direct-map 区域调 `g_vmm.map(virt=phys+DIRECT_MAP_BASE,…)` → `walk_level` 命中 direct-map 的 **1GB huge entry（PDPT[0]=0x83，PS bit）** → 触发 huge-split → 把 `pdpt[0]` 改成 4KB PT pointer → **破坏全局 direct-map** → 后续 `phys_to_virt` walk 错乱命中 phys 0 BIOS 数据当 PT → reserved PF（err=0x9，`test_cache_reads_real_file` 首次 AHCI init 触发）。诊断证实建后/load_elf 后页表正确（`PML4[272]=0x10003 PDPT[0]=0x83`），破坏在运行时。M3 时 DmaPool virt 在 KERNEL_VMA 窗口（2MB/4KB）map 无害，批2 迁 direct-map 后旧 map 变破坏源（迁移漏删）。修：删 `DmaPool::alloc` 的 `VMM.map`（direct-map 已被 loader 永久 identity 映射，不需 map，与 `return_pages` 只 free phys 不 unmap 对称，GOTCHA#7/#13）；`test_dma_pool` Test1 改 CPU round-trip（原 translate 断言依赖 split 副作用）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `DIRECT_MAP_BASE` 常量 + mini `direct_map_up_to`（2MB/1GB 大页 identity，PT@0x10000）+ loader 调用 + main_test identity 探针 | ✅ | f0b84ed | 734/0 + 探针 OK |
| 批2 | centralize `phys_to_virt`→DIRECT_MAP_BASE（删 4 重复）+ 迁移 direct-map 站点（page_cache/dma_pool/execve/usermode/process_new）+ 保留 kernel-image KERNEL_VMA + 测试跟进 | ✅ | 93ac379 | 增量 734/0 + host 49/0 + 实机不炸（**注**：fresh build 暴露 Bug1，此为增量态）|
| 批3 | **Bug1 修复**：删 `DmaPool::alloc` 的 `VMM.map`（walk_level split 1GB huge 破坏 direct-map）+ `test_dma_pool` Test1 translate→CPU round-trip + return_pages 注释订正 | ✅ | (本次) | **fresh build 734/0** |

**完成总结**：direct-map 独立窗口落地——loader 用 1GB（PDPE1GB）/2MB 大页把全 RAM identity 映到 `DIRECT_MAP_BASE`（PML4[272]，PT@0x10000），`phys_to_virt` 切到新窗口，全栈 direct-map 站点迁移。修 latent >1GB bug（页表/page_cache/DMA/execve/GS 对 high phys 现正确 identity）。关键教训：`-cpu max` 仅 KVM 时设→qemu64 无 PDPE1GB，须 2MB fallback；迁移严判 direct-map vs kernel-image；**direct-map 区域勿 `VMM.map/unmap/translate`**（walk_level 遇 1GB huge 会 split 破坏全局 direct-map，DmaPool 迁移漏删 map 教训，见 GOTCHA#13）。buddy 接 PMM（F2-M7 兑现）见下「F2-M7 Buddy PMM」段（批4a/4b，2026-06-18 完成，fresh KVM 742/0 + GUI 冒烟）。direct-map 前置就绪。

## ✅ F2-M7（Buddy PMM）已完成 — 2026-06-18（fresh KVM 742/0 + 实机 GUI 冒烟）

> 目标：buddy 伙伴系统替换 PMM flat bitmap（power-of-two order free lists），兑现 M7。direct-map 前置（上段）就绪。
> 决策：
> - **批4a**：cherry-pick buddy 批1 + `page_to_block`/`pop_free` KERNEL_VMA→DIRECT_MAP_BASE + low-first（fresh 742/0）。
> - **批4b**：PMM bitmap→buddy wiring（init/alloc/free/count + order_/bitmap_storage + `_locked` 锁契约）。
> - **Bug2 修正（批4b 核心）**：初版侵入式 free-list（next 指针写 free 页头经 direct-map）在 **WSL2 nested KVM（AMD）EPT 写读不一致** → free-list 链路振荡（valid↔poison `0xCAFEBABEDEADC0DE`）→ pop_free 遍历 #GP（`test_wm_close_button_closes_terminal_pipes`）。**TCG 2MB path 742/0 全绿**证明 buddy wiring 本身正确，根因是 nested KVM 物理层。**修 = buddy 改非侵入式 per-order bitmap free-list**（bitmap 存 metadata 区，不写 free 页，KVM nested safe；`find_first_set` 天然 low-first）。GOTCHA#14。
> 不做：SLAB（M7b 后续）/ CoW 共享内存（F3）/ 脏页写回。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批4a | cherry-pick buddy 批1 + `page_to_block`/`pop_free` 迁 DIRECT_MAP_BASE + low-first + test_buddy 适配 | ✅ | 2ad5442 | fresh 742/0（734+8 buddy 单测）|
| 批4b | PMM bitmap→buddy wiring（init/alloc/free/count + order_/bitmap_storage + `_locked`）+ **Bug2 修**：侵入式→bitmap free-list（nested KVM safe）+ test_buddy 适配 + qemu.cmake `CINUX_NO_KVM` env（TCG 诊断） | ✅ | (本次) | **fresh KVM 742/0 + 实机 GUI 启动到桌面** |

**完成总结**（F2-M7）：buddy 伙伴系统替换 PMM flat bitmap——`BuddyAllocator`（**per-order bitmap free-list**，1 bit/block，`find_first_set` 天然 low-first，非侵入式不写 free 页）+ PMM 接入（`init` 用 `buddy.init`+`mark_free_region` 排除 kernel/metadata 区；`alloc_page[_locked]`/`alloc_pages`/`free[_pages]`/count 调 buddy；`_locked` 保 IF=0 锁契约）+ order_ 数组（1B/页，head order 权威）+ bitmap_storage（per-order bitmap ~575KB @ order_storage 后）。关键教训 **GOTCHA#14**（nested KVM EPT 对 intrusive free-list 写读不一致 → 改 bitmap metadata 解；TCG 验证 buddy 逻辑正确）。验证：fresh KVM run-kernel-test 742/0 + 实机 `make run` GUI 启动到桌面不崩。遗留：SLAB（M7b）/ CoW（F3）。

## ✅ F2-M7b（SLAB 分配器）已完成 — 2026-06-18（fresh KVM 752/0 + host 48/0 + GUI 冒烟）

> 目标：buddy 之上分层小对象分配器 `SlabAllocator`，**全替 Heap**（不留 fallback「尾巴」），闭环 PMM(buddy)→Slab→kmalloc/kfree→operator new。小对象(≤2KB)走 Slab（9 通用缓存 + 专用缓存）；大对象(>2KB / 大对齐)走 **buddy + direct-map 复用**（DmaPool 同款 GOTCHA#7/#13，零 map/零元数据）。
> 决策（propose 已确认，2026-06-18）：
> - **#1 全替 Heap**：`kmalloc` 通用（小→Slab / 大→buddy+direct-map），Heap 删，无 fallback。
> - **#2 大对象复用 direct-map**：`virt=phys+DIRECT_MAP_BASE`，不 map/unmap；`kfree` 用 `phys=virt-DIRECT_MAP_BASE`+`free_pages`（buddy 记 order 权威，count 忽略）。免 KMEM_LARGE 区。
> - **#3 Slab 页 4K 映射**：侵入式 freelist 写在 4K 页内（KMEM_SLAB 区，PML4[256]），**绝不 direct-map huge**（GOTCHA#14/#15）。empty slab 可回收。
> - **#4 专用缓存现在做**：Task/Inode/VMA/CachedPage（现存类型）；Dentry 弃（不存在）；Pipe 走通用缓存。
> - **#5 Slab 返 nullptr on OOM**（operator new 链，freestanding 无异常，同 Heap 契约，非 ErrorOr）。
> - **#6 IF=0 安全**：PF handler(IF=0) 的 `new CachedPage`→kmalloc→Slab，须 IF=0 安全（批1 核 Heap::alloc 现状定 Slab 锁契约）。
> 删除面（批2）：`heap.{hpp,cpp}` + `test_heap.cpp` + CMake 行；改 `crt_stub.cpp`(7 重载)/`main.cpp:138`/`test/main_test.cpp:151`/`ram_block_device.hpp`(直接 g_heap.alloc/free 大块存储)。
> 不做：Heap 保底（全删）、Dentry 缓存、SLAB 着色/NUMA、性能 benchmark 套件（批4 仅方向断言）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `slab.hpp/cpp`（SlabAllocator 8 通用缓存 16-2048B + 页内 header + 侵入式 freelist + 4K 页 on-demand grow + KMEM_SLAB 区 + irq_guard IF=0）+ memory_layout KMEM_SLAB + CMake + `test_slab.cpp` 单测 | ✅ | 563bb0f | fresh KVM 750/0（742+8）+ host 49/0 |
| 批2 | `kmalloc/kfree`（小→Slab / 大→buddy+direct-map，free 按区路由）+ `crt_stub` 7 重载 + main/main_test init + `ram_block_device` 迁移 + **删 heap.{hpp,cpp}/test_heap(内核+host)** + slab 硬化（grow 页零化 + O(1) double-free 毒检）+ `test_kmalloc.cpp` 11 测 + `test_slab` +double-free 测 + **page_cache 按 `ino` 键控**（修 slab 复用 Inode 地址暴露的陈旧命中，GOTCHA#15） | ✅ | 4e05892 | **fresh KVM 751/0 + host 48/0 + 实机 GUI 到桌面** |
| 批3 | 专用缓存 API（`create_cache`/`cache_alloc`/`cache_free`，`kObjAlign`=16 支持非幂 obj_size）+ `dedicated_caches.cpp`（task/vma/cached_page，类专属 operator new/delete 自动路由，无调用点改动）+ `test_slab` +dedicated 测。**Inode 不入 slab**：ext2 用固定 `inode_cache_[]` 数组自管（非堆分配，N/A）。实测 Task=1008B→4/slab(原 1024 档 3)、VMA=56B→72/slab(原 64 档 63) 真省碎片；CachedPage=64B(2幂,接线为完整) | ✅ | 5d932e8 | **fresh KVM 752/0 + host 48/0 + 实机 GUI 到桌面** |
| 批4 | 收尾：ROADMAP/PLAN/todo/notes + GOTCHA + fresh KVM run-kernel-test + `test_host` + `make run` 冒烟 | ✅ | (本次) | fresh KVM 752/0 + host 48/0 + GUI 到桌面 |

**完成总结**（F2-M7b，4 批）：SLAB 分配器全替 Heap——批1 `SlabAllocator`（8 通用缓存 16-2048B + 页内 header + 侵入式 freelist + 4K 页 on-demand grow，KMEM_SLAB 独立 4K 区 PML4[256]，irq_guard IF=0 安全）；批2 `kmalloc/kfree`（小→Slab / 大→buddy+direct-map 复用，零 map/零元数据）+ crt_stub 7 重载 + ram_block_device 迁移 + **删 heap.{hpp,cpp}/test_heap（内核+host，净 -1951 行）** + slab 硬化（grow 页零化 + O(1) double-free 毒检）+ **修 page_cache 按 `ino` 键控**（slab 复用 Inode 地址暴露的陈旧命中 → sys_read 返错字节，GOTCHA#15）；批3 专用缓存 API（`create_cache`/`cache_alloc`/`cache_free`，`kObjAlign`=16 支持非幂 obj_size）+ dedicated_caches（task/vma/cached_page，**类专属 operator new/delete 自动路由**，无调用点改动）——实测 Task=1008B→4/slab(原 3)、VMA=56B→72/slab(原 63) 真省碎片；Inode 不入 slab（ext2 固定 `inode_cache_[]` 自管，N/A）。

架构：A.6 边界（Slab/kmalloc 返 nullptr on OOM，非 ErrorOr）；A.7 不入 Cinux-Base（依赖 PMM/VMM/heap）；侵入式 freelist 写 4K 页（绝不 direct-map huge，GOTCHA#13/#14）；大对象复用 direct-map（DmaPool 同款 GOTCHA#7/#13）；类专属 operator new/delete 让专用缓存对调用点透明（错误路径自动覆盖）。

关键教训 **GOTCHA#15**（slab 复用暴露按指针键控的缓存——page cache 按 `Inode*` 键控 → 新文件命中陈旧页 → `sys_read` 返错字节；Heap first-fit 侥幸不复用同地址故潜伏。改按 `inode->ino` 稳定号键控。**通用铁律**：按对象指针/地址键控的在线结构（cache/table），对象经分配器回收时必然陈旧命中，键须用稳定 id/number 非 pointer）。

验证：fresh KVM run-kernel-test 742→**752/0**（+10 slab/kmalloc/dedicated 单测 - 旧 heap 测 + page_cache 修复回归）+ host test_host **48/0** + 实机 `make run` GUI 启动到桌面不崩。**F2 内存管理增强里程碑收官（M1-M7 + M7b）**。

## ✅ F2-M6（ext2 Cache）已完成 — 2026-06-17

> 目标：`sys_read` 对磁盘文件走 PageCache，与 demand paging 共用 `(Inode*, page_offset)` 缓存——重复读命中免读盘，闭环读路径唯一缓存层。M4 PageCache 此前只服务 file-backed mmap 的 PF 路径，read() 直走 Ext2FileOps 每块读盘无缓存。
> 决策（propose 已确认）：
> - **#1 read() 走 read_bytes**（复用 M4 get_page，不另起缓存逻辑）。
> - **#2 判别用 virtual `is_page_cacheable()`**（pipe type 也是 Regular，禁 RTTI；默认 false，Ext2FileOps override true，源码兼容现有 mock）。
> - **#3 read_bytes 切片按页对齐**，EOF 以 `inode->size` 截断（ext2 lookup 已填），partial-read 中途失败回已读量。
> 不做：脏页写回 / MAP_SHARED 写一致性 / LRU 淘汰 / 跨进程 CoW（留后续/F3）。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | `PageCache::read_bytes`（按页切片经 get_page + EOF 截断）+ `InodeOps::is_page_cacheable()` virtual（默认 false）+ Ext2FileOps override true + test_page_cache 3 测（基本+EOF / 二读命中缓存 / 跨页裁剪） | ✅ | ca13352 | 733/0（+3） |
| 批2 | `sys_read` 分流（is_page_cacheable 真→read_bytes，否则原 read；pipe/ramdisk 不变）+ test_syscall_ext2 端到端测（真 AHCI/ext2 读 /hello.txt 两遍 hit_count 升+字节一致） | ✅ | 3a24439 | 734/0（+1） |
| 收尾 | 文档(本文+ROADMAP+todo) + 全量 run-kernel-test + host test_host + 实机冒烟 | ✅ | (本次) | 734/0 + host 49/0 + 实机不炸 |

**完成总结**（730→734，F2-M6 +4）：read() 缓存路径落地——`PageCache::read_bytes(inode,off,buf,count)`（按页对齐切片复用 M4 `get_page`：命中免读盘 / 未命中填充+EOF 零填，EOF 以 `inode->size` 截断，partial-read 回已读量）+ `InodeOps::is_page_cacheable()` virtual（默认 false，Ext2FileOps override true；pipe type 也 Regular 故 type 不可靠，禁 RTTI 用 virtual 判别）+ `sys_read` 分流（cacheable→read_bytes，否则原 inode->ops->read，pipe/ramdisk 不变）。架构：A.6 ErrorOr；避免递归（read_bytes 新函数，Ext2FileOps::read 读盘原语不改）；翻译边界 errno 不变。验证：批1 单测（read_bytes 缓存机制）+ 批2 真机端到端（AHCI/ext2 读两遍 hit_count 升 = sys_read→read_bytes 接线铁证）+ host test_host 49/0（InodeOps virtual 源码兼容）+ 生产内核启动到 GUI 桌面不炸。遗留：脏页写回 / MAP_SHARED 写一致性 / LRU 淘汰 / 跨进程 CoW 留后续（F3）。

## ✅ F3-M1（信号系统）已完成 — 2026-06-18（fresh 783/0 + 实机 GUI 冒烟）

> 目标：从零构建 POSIX 信号（核心 22 个）：投递/处理/屏蔽 + Custom handler round-trip + kill/sigaction/sigprocmask/sigreturn + PF→SIGSEGV/exit→SIGCHLD/write→SIGPIPE 集成。解锁 TTY/shell（依赖瓶颈）。
> 决策（propose 已确认）：
> - **#1 Custom 走中断路径，不改 syscall.S**：当时 syscall.S 精简帧不能 sigreturn 完整恢复；Custom 在中断/异常返回路径投递（ISR 宏 `signal_check_deliver_isr`），sigreturn 经 IDT vector 0x80 trap gate（DPL=3）收完整 InterruptFrame。syscall 路径只 Default/Ignore。2026-06-27 后 syscall frame 已扩到 128B，但 Custom signal 仍沿中断完整帧路径。
> - **#2 栈注入 trampoline**：handler 返回地址指向栈上 `int $0x80`（cd 80）。依赖 NXE 关闭（GOTCHA#10），F9 启用迁 vdso。
> - **#3 MVP 范围**：核心 22 信号 + Default/Ignore/Custom；实时信号/sigaltstack/SA_RESTART/嵌套/STOP-CONT 真效果留后续。
> - **#4 SIGCHLD + waitpid non-blocking**：exit 投 SIGCHLD（default Ignore），waitpid 轮询，阻塞唤醒留 TODO（wait_queue 同 F3-M2）。
> 不做：实时信号、job-control 真调度、waitpid 阻塞、进程组 kill（F3-M3）、F9 NXE 后 trampoline 迁 vdso。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | signal.hpp（Signal enum 22 + SigSet ops + SigAction + default/uncatchable 查表）+ Task 信号字段 + fork 继承（清 pending）+ 单测 | ✅ | 860cf86 | 770/0（+7）|
| 批2 | signal.cpp（send/pick/exec_default/check_and_deliver + pid→Task 注册表）+ kill/sigaction/sigprocmask + syscall_dispatch 挂载 + 单测 | ✅ | bd558c8 | 780/0（+10）|
| 批3 | SignalFrame + signal_setup_frame + sigreturn_handler + 中断路径投递（ISR 宏 signal_check_deliver_isr）+ IDT vector 0x80 gate + int $0x80 trampoline + 单测 | ✅ | f9f0e9a | 782/0（+2）|
| 批4 | 集成：PF→SIGSEGV（handle_pf）+ exit→SIGCHLD（sys_exit）+ write→SIGPIPE + registry 单测 | ✅ | c623fbb | 783/0（+1）|
| 批5 | 收尾：libc signal wrapper（syscall.h/.cpp）+ 文档（PLAN/ROADMAP/todo/notes/GOTCHA）+ 实机冒烟 | ✅ | (本次) | 783/0 + GUI 启动到桌面 |

**完成总结**（763→783，F3-M1 +20）：POSIX 信号落地——`Signal`（22 核心号）+ `SigSet`（64-bit bitmask）+ `SigAction`（Default/Ignore/Custom）+ Task 信号字段（sig_actions[23]/sig_pending/sig_blocked，fork 继承清 pending）+ 投递（`signal_send` 设 pending / `signal_pick_deliverable` 选信号 / `signal_exec_default` 默认动作 / `signal_check_and_deliver` syscall 路径 / `signal_check_deliver_isr` 中断路径）+ pid→Task 注册表（sys_kill 查找）+ signal frame（用户栈构造 + `int $0x80` trampoline）+ sigreturn（vector 0x80 gate 收 InterruptFrame 恢复）+ 集成（PF→SIGSEGV / exit→SIGCHLD / write→SIGPIPE）+ libc wrapper（sys_kill/sys_sigaction/sys_sigprocmask）。架构：A.6 边界（signal_send 返 -errno）；syscall handler 6 参；Custom 投递挂中断返回路径（避开 syscall.S 精简帧 sigreturn 限制）；栈注入 trampoline（NXE 关闭可行，F9 迁 vdso）。

关键教训 **GOTCHA#16**（sigreturn 栈注入依赖 NXE 关闭 + Custom 走中断路径 + signal_check_deliver_isr 严判 cs&3）。

验证：fresh run-kernel-test 763→**783/0**（+20 signal 单测）+ 实机 `make run` 生产内核启动到 GUI 桌面（Desktop icons / gui_worker），signal_check_deliver_isr 在每个中断（PIT/AHCI）后高频跑全程不炸，kernel_init exit→SIGCHLD 投递不炸。**Custom handler 真用户态 round-trip 留后续**（libc wrapper 已就绪，需用户程序触发）。下个焦点：F3-M2 clone + futex + TLS。

## ✅ F3-M2（线程支持：clone + futex + TLS）已完成 — 2026-06-18（5 批，783→810）

> 目标：为 musl/pthread 打内核地基——`clone`(56) Linux 风格线程原语 + `futex`(202) 用户态互斥 + TLS（FS 段基址 MSR_FS_BASE）+ 线程组（tgid / CLONE_CHILD_CLEARTID exit futex_wake）。
> 决策（propose 已确认）：
> - **#1 共享资源 refcount 指针化（非 MVP 拷贝）**：sig_actions / fd_table / cwd 都重构为引用计数共享对象。CLONE_SIGHAND/FILES/FS 真共享（POSIX 正确，musl pthread 带这些 flag）；fork 仍 copy 语义（fork 不带 CLONE_*，子建新对象）。代价：批3 重构侵入 signal.cpp/fork/所有 sig_actions+cwd 访问点，但批4 clone 才能正确共享。
> - **#2 futex = WAIT/WAKE + BITSET**（多 uint32 掩码匹配，pthread 条件变量用）；**不做** timeout（需 PIT timer 定时唤醒）/ PI / requeue。
> - **#3 clone 子进程用户栈返回**：复用 fork「拷父核栈 + relativize + trampoline」机制，patch 拷贝帧的 user_rsp 槽 = `stack` 参数（syscall.S 帧 `rsp+0=user_rsp`，via fork delta 相对化定位）。备选 Approach C（clone_child_trampoline 直跳用户态复用 jump_to_usermode）若 patch 太脆。
> 不做：futex timeout/PI/requeue、实时信号线程语义、job-control 真调度、进程组 kill（F3-M3）。
> 依赖就绪：M1 信号（mask 继承）、F2 mmap（线程栈）、scheduler block/unblock、Spinlock、ErrorOr、next_tid、per_cpu。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | **TLS（fs_base）**：`CpuContext` 加 `fs_base`(offset 80, alignas(16) 填充 sizeof 80→96)+static_assert；context_switch.S 存/恢复 fs_base(MSR_FS_BASE=0xC0000100) 与 gs_base 同段；`kernel/arch/x86_64/tls.{hpp,cpp}` `cinux::arch::set/get_tls_base`(wrmsr/rdmsr)；task_builder 新核线程显式 fs_base=0；单测 round-trip（值须**规范地址**否则 wrmsr #GP）。**GOTCHA：FS_BASE 须规范地址** | ✅ | 8f2805b | 785/0（+2） |
| 批2 | **futex**：`sys_futex.{hpp,cpp}` 256-bucket hash + 侵入式 Waiter(`Task::wait_next`)+Spinlock（镜像 Mutex/Semaphore 的 block/unblock 范式）；FUTEX_WAIT(直接 deref `*uaddr`，≠val 返 EAGAIN，入队→Scheduler::block，醒后出队返 0)/WAKE(出队最多 val→unblock，返唤醒数)/BITSET(uint32 掩码匹配)；Task 加 futex_uaddr/futex_bitset；注册 SYS_futex=202；单测。**陷阱：测试用 g_per_cpu.current（不 set_current）避 block 挂死；全局表跨测试残留须每测 wake 清理** | ✅ | 2f1c331 | 794/0（+9） |
| 批3 | **共享资源 refcount 基建 + retrofit fork**：`SharedSigActions`(refcounted 堆对象持 SigAction[23]，Task 指针化，signal.cpp/fork/所有 `sig_actions[n]` 访问改 `->actions[n]`) + `FDTable` 加 refcount(acquire/release) + `SharedCwd`(refcounted 指针化，getcwd/chdir/execve/fork 全访问点改)；fork copy 语义建新对象（memcpy 后立即 create_copy，error-path delete 不碰父）；operator delete 经 release_resources 释放三者；acquire/release + 单测。**陷阱：fork memcpy 拷指针须立即 detach+copy；Task slab 无 ctor 须手工分配；测试 Task t{} 须手动 create** | ✅ | 5a8d251 | 801/0（+7） |
| 批4 | **线程组 + clone 核心**：Task 加 `tgid`/`group_leader`/`clear_child_tid`/`set_child_tid`；`sys_getpid` 返 tgid；`sys_clone.{hpp,cpp}`+clone()（复用 fork 拷核栈机制）；CLONE_VM/FILES/SIGHAND/FS(共享 批3 对象)/THREAD(tgid=父 tgid,兄弟)/SETTLS(设 fs_base)/SETTID/CLEARTID(记地址)；**子进程用户栈返回**（patch 帧 user_rsp 槽，GOTCHA#18；2026-06-27 后 frame size=128B）；fork/clone 加 full_used 上限防御；注册 SYS_clone=56；test_clone 8 测 + 修 test_fork_exec（rsp+4096+remove_task）+ getpid 测试设 tgid。**陷阱：getpid→tgid 使旧 getpid 测试断言失败→TEST_ASSERT 早 return→跳过 set_current→current_ 悬垂崩（非踩踏）** | ✅ | 8808c2c | 809/0（+8） |
| 批5 | **集成 cleartid + libc + 收尾**：`task_exit_cleartid`（线程退出写 0 到 clear_child_tid + `futex_wake_addr` 唤醒 joiner，sys_exit 调用）；futex_wake_addr 暴露内核内部 wake；libc `sys_clone`/`sys_futex` wrapper + `_syscall5`/`_syscall6`(r10/r8/r9) + CLONE_*/FUTEX_* 常量；cleartid 单测。**真用户态线程 round-trip + 实机 GUI 冒烟留 follow-up**（需用户线程程序） | ✅ | fe7b535 | 810/0（+1） |

**架构契合**：A 翻译边界（clone/futex 返 -errno：EAGAIN/EINVAL/ESRCH/ENOMEM，内核内 ErrorOr，仅 trap 入口翻 errno）；A 禁 RTTI（按字段共享，无 dynamic_cast）；A.7 不入 Cinux-Base（依赖 PMM/VMM/scheduler/heap）；层化 arch(tls)/proc/scheduler/syscall 各司其职不反向依赖；对齐 Linux（clone flags/FUTEX op/线程组语义，CONFIG 风格不重造轮子）。

**风险**：
- **R1（最高）clone 子进程用户栈返回**：syscall.S 帧 `rsp+0=user_rsp / rsp+8=user_rip`，子进程拷来的帧须把 user_rsp 槽 patch 成 `stack` 参数（用 fork 现成 delta 相对化 `(child_addr = parent_slot - current_rsp + child_stack_start)`）。批4 实测验证；备选 Approach C（clone_child_trampoline 直跳用户态，复用 jump_to_usermode）。
- **R2 共享 refcount 生命周期**：线程 exit 不释放共享表（fd/sig/cwd），仅 group-leader 进程 exit 最后 release 到 0 才释放。retrofit fork 须保 fork copy 语义不破现有测试。
- **R3 启动路径**：Task 结构变（新字段 + sig_actions/cwd 指针化）+ context_switch.S 变 → run-kernel-test 全 kernel-mode 不覆盖 ring3 线程路径，实机冒烟必做（GOTCHA#11 同类）。
- **R4 futex 无 timeout**：FUTEX_WAIT 无限等，waker 必须存在；测试用 2 task（一 wait 一 wake）配对。

**GOTCHA（新增预留）**：#18 clone 子进程用户栈返回（帧 user_rsp 槽 patch）+ 共享 refcount 生命周期（线程 exit 不释放共享表，进程 exit 最后释放）。批1 已落 GOTCHA#17（wrmsr FS_BASE 须规范地址）。

## ✅ F3-M3（进程组/会话 + waitpid 阻塞）完成 — 2026-06-19（5 批，810→827）

> 目标：为 Job Control / TTY 打地基 —— 进程组（pgid）+ 会话（sid）+ `setpgid`/`getpgid`/`getsid`/`setsid`/`killpg` + fork 继承，**顺带补 waitpid 阻塞**（闭环 F3 进程管理，shell/busybox 不再忙等烧 CPU）。
> 决策（propose 已确认）：
> - **#1 范围 = 进程组 + waitpid 阻塞**（非纯 todo 原样，也非全包 STOP/CONT 真调度）。STOP/CONT 真调度留 M4 调度器。
> - **#2 继承规则方案 A + 可测 helper**：`inherit_process_identity`（process_internal.hpp），root fork（`parent->pgid==0`）自成组，否则继承父。fork/clone memcpy 后显式调用。init(pid=1) 自动满足（kernel_init pid=0 fork 出 pid=1 自成组）。
> - **#3 killpg 放 signal.cpp**（持 `g_registry_head` + `signal_send`，遍历按 pgid 广播）；process_group.cpp 只做纯字段 setpgid/getsid/setsid。
> - **#4 waitpid 阻塞复用 `Scheduler::block/unblock` + exit 唤醒 parent**，不引入新 wait_queue。
> - **#5 controlling_tty 只占位**（-1），真控制终端 attach 留 F10-M3 TTY。
> 不做：SIGSTOP/CONT 真调度（M4）、实时信号组语义、权限检查（全 root）。
> 复用现成基建：pid registry（`signal_find_task_by_pid` + `g_registry_head`）、Task children/parent、`Scheduler::block/unblock`（futex 同款）、`signal_send`。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | Task 加 `pgid`/`sid`/`session_leader`/`controlling_tty` + `inherit_process_identity`（root 自成组 / 否则继承父）+ fork/clone 调用 + 5 单测 | ✅ | 77be415 | 815/0（+5） |
| 批2 | `process_group.{hpp,cpp}`：setpgid/getpgid/getsid/setsid（纯字段语义）+ killpg（signal.cpp 遍历 registry 按 pgid 广播）+ sys_kill pid<0 接 killpg 闭环 TODO + 9 单测 | ✅ | 824449c | 824/0（+9） |
| 批3 | 4 syscall（setpgid=109/setsid=112/getpgid=121/getsid=124）+ 注册 + libc wrapper + 3 端到端单测 | ✅ | b228f67 | 827/0（+3） |
| 批4a | **exit Dead→Zombie 契约修正**：sys_exit Zombie + dequeue（对齐 exit_current）+ schedule(275) 跳 Zombie（pick_next 不查 state，Zombie 留 queue 会崩） | ✅ | ee13cac | 827/0 回归 + 实机 GUI 到桌面 |
| 批4b | **waitpid 阻塞**：默认 block、`WNOHANG` 非阻塞；exit 唤醒 waiting parent；terminal/test 全改 WNOHANG 防挂死 | ✅ | 734d6a1 | 827/0 + 实机 GUI 到桌面 |
| 批5 | 收尾：文档（PLAN/ROADMAP/todo/notes/GOTCHA）+ 全量验证 + 实机冒烟 | ✅ | (本次) | 827/0 + host + 实机 GUI 到桌面 |

**风险（propose 预判）**：
- **R1（最高）批4 破现有 waitpid 测试**：默认行为从 non-blocking 变 blocking，现有 `test_fork_exec` 等若没传 `WNOHANG` 且无 zombie → 挂死。批4 第一步 grep 全部 waitpid 调用点审计（GOTCHA#19 同款家族 + futex 单测 block 挂死坑）。
- **R2 exit 唤醒**：`sys_exit` 在 scheduler 路径，`unblock(parent)` 要在 block/unblock 锁契约内（IF=0 / irq_guard），参考 futex wake。
- **R3 killpg 迭代安全**：遍历 registry 广播时 task 可能正退出 → 持锁或先收集 pid 再发。

## ✅ F3-M4（调度器接口验证与增强）完成 — 2026-06-19（5 批，827→840）

> 目标：验证 SchedulingClass 插拔接口完备性 + 小幅增强(优先级/多类),并兑现 M3 留的
> "SIGSTOP/CONT 真调度效果(TASK_STOPPED 状态机)"。**不引入新调度器实现**(todo 铁律),
> 向后兼容(生产单类场景行为不变)。
> 决策（propose 已确认）：
> - **#1 T1 接口钩子给默认实现**(非纯虚):task_tick/task_fork/task_deadline 基类默认
>   no-op/false/0,现有子类零改动;时间片量子从 `Scheduler::current_slice_` 移入 RoundRobin
>   (`quantum_remaining_`),`tick()` 委托给类——抢占策略内聚。
> - **#2 T2 优先级小值优先**:pick_next 扫描选 `priority` 最小者,并列取最早入队(FIFO),
>   故同优先级 RR;严格优先级(可饿死),对齐 todo「简单实现」。
> - **#3 T3 pick_next_from(classes,count) 公开原语**:数组入参,脱离全局 `default_rr_` 残留态
>   单测(传本地类数组);schedule/exit_current/run_first 三处 `default_rr_.pick_next()` 改走
>   `pick_next_task()`(绑全局 classes_),让多类机制真正生效。注册序即优先级(不加 class-priority 参数)。
> - **#4 STOP/CONT 发送时恢复(关键)**:Stopped 任务永不被调度无法自投递 SIGCONT/SIGKILL,
>   故 `signal_send` 见 Stopped+(SIGCONT|SIGKILL) 立即 Ready+enqueue;SIGCONT 另清 pending stop。
>   schedule() 守卫加排除 Stopped。
> 不做:CFS/防饿死、task_fork 接 fork 路径(memcpy 已拷 priority,等价 noop)、
> SIGKILL/SIGTERM 唤醒 Blocked(可中断睡眠,更大改动)、waitpid 报告 stopped(WUNTRACED)。

| 批 | 范围 | 状态 | Commit | 测试 |
|----|------|------|--------|------|
| 批1 | **T1 接口钩子**:SchedulingClass 加 task_tick/task_fork/task_deadline(基类默认实现)+ RoundRobin 接管量子(task_tick 2-tick 抢占 + 重充,pick_next 重置,task_fork 继承 priority)+ tick() 委托 + 删 current_slice_ | ✅ | 8b6c46e | 830/0(+3) |
| 批2 | **T2 优先级 RR**:pick_next 扫描选 priority 最小者(并列 FIFO→同级 RR)+ 抽 remove_at_locked 助手共享环形紧凑(dequeue/pick_next 复用) | ✅ | f3b0493 | 833/0(+3) |
| 批3 | **T3 多类查询**:pick_next_from(classes,count) 公开原语 + pick_next_task() 私有包装;schedule/exit_current/run_first 改走它(不再绕过 classes_[]) | ✅ | d57bb41 | 836/0(+3) |
| 批4 | **STOP/CONT 真调度(M3 follow-up)**:TaskState::Stopped + signal_exec_default kStop(Stopped+dequeue+maybe schedule)/kContinue(恢复) + signal_send 发送时恢复(SIGCONT/SIGKILL)+清 pending stop + schedule 守卫排除 Stopped | ✅ | e9b0dd4 | 840/0(+4) |
| 批5 | 收尾:T4 头部伪代码(pluggable scheduling 示例)+ ROADMAP/PLAN/todo/notes + fresh run-kernel-test + `make run` 实机冒烟 | ✅ | (本次) | 840/0 + 实机 GUI 到桌面 |

**架构契合**：A 翻译边界(signal_send 返 -errno,内核内直接改状态);A 禁 RTTI(SchedulingClass virtual 多态);A.7 不入 Cinux-Base(依赖 Task/Scheduler);对齐 Linux(priority 小值优先、SIGCONT 发送时恢复 + 清 pending stop、CONFIG 风格不重造轮子)。

**完成总结**（827→840，F3-M4 +13）：调度器插拔接口完备化 + STOP/CONT 状态机落地——①SchedulingClass 三策略钩子(默认实现,时间片内聚到类,删 current_slice_ 单一事实源在 RoundRobin::quantum_remaining_);②优先级感知 RoundRobin(pick_next 选最小 priority,同优先级 FIFO 轮转,remove_at_locked 助手去重);③多调度类实际查询(pick_next_from 数组原语让遍历可单测,schedule/exit/run_first 不再绕过 classes_[],生产单类场景等价=向后兼容);④SIGSTOP/SIGCONT 真调度(TaskState::Stopped 状态机,signal_send 发送时恢复 Stopped 目标 + 清 pending stop,schedule 守卫排除 Stopped)。T4 头部伪代码示例(如何加新调度算法:继承 SchedulingClass + register_class)。

关键教训 **GOTCHA#22**(TaskBuilder 消耗全局 tid 计数器跨测污染——批4 首版用 TaskBuilder 建 victim 分到 tid 1/2/3,致 test_build_basic_task 的 tid==1 断言失败;改栈 Task t{} 零消耗解。**通用铁律**:跨测试文件共享全局计数器,新测试用 TaskBuilder 建任务会位移他测「首任务 tid==N」断言;纯状态机测试用栈 Task)。

验证：fresh run-kernel-test 827→**840/0**(+13 接口/优先级/多类/stop-cont 单测 - 0 删,827 回归全绿=向后兼容铁证)+ 实机 `make run` GUI 启动到桌面不崩(kernel_init/gui_worker 经新 pick_next 路径,无 panic/halt)。**诚实记录**:① schedule/exit/run_first 改写靠 827 回归覆盖(高危路径);② STOP/CONT「停止当前任务→schedule 切走」路径无法单测(context switch 同 block/futex 坑)+ 生产启动无 SIGSTOP 投递(实机也覆盖不到)→ 靠逻辑正确性(复用 block 范式)+ 留真 shell job-control 程序端到端验证。**F3(进程与线程)全里程碑收官(M1-M4 ✅)**。
