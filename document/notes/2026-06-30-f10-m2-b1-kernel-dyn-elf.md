# F10-M2 批1 — kernel 动态 ELF 加载(PT_INTERP + interp + auxv AT_BASE)

> 2026-06-30。接 F10-M1 musl 静态移植。本批让 execve 识别动态 ELF,把内核侧
> 动态链接的地基铺好(批2 加 musl 动态工具链,批3 端到端 smoke)。
> 分支 `worktree-f10-m2-dynlink`(从干净 main `1cdd507`)。commit `b55615e`。

## 背景 / 目标

F10-M1 的 execve 只认静态 ELF(ET_EXEC,无 PT_INTERP)。动态链接要 kernel 做的最小集:
① 主程序 phdr 扫到 PT_INTERP → 读 interp 路径 ② 把 interp(ldso)ELF 当 ET_DYN 映到某 base
③ entry 改 interp 入口、auxv 喂 AT_BASE/AT_ENTRY/AT_PHDR。GOT/PLT/DT_NEEDED/符号解析全由
musl ldso 在用户态干——**kernel 不自建 loader**(用户决策,对齐 Linux,libc 解耦)。

## 调研实证(拉 musl-1.2.5 源码到 /tmp 坐实,不猜)

- **interp 路径串** = `/lib/ld-musl-x86_64.so.1`(Makefile `LDSO_PATHNAME = $(syslibdir)/ld-musl-$(ARCH)$(SUBARCH).so.1`)。
- **shared 默认开**(configure `--disable-shared [enabled]`)→ 现有 build-musl.sh 已自带 libc.so。
- **ldso(ET_DYN)靠 `__ehdr_start` 定位自身**(`ldso/dynlink.c`)→ kernel 只需把它在某 base 连续映上去。
- **ldso 读的 auxv**:`AT_PHDR/PHNUM/PHENT`(主程序 phdr VA,ldso 用 `AT_PHDR - PT_PHDR.p_vaddr` 算主程序 base)、`AT_ENTRY`(主程序 entry,重定位完 `CRTJUMP(aux[AT_ENTRY])` 跳过去)、`AT_BASE`(ldso base)、`AT_PAGESZ`、`AT_UID/EUID/GID/EGID/AT_SECURE`、`AT_HWCAP`、`AT_EXECFN`、`AT_RANDOM`。
- **ELF 常量**:PT_DYNAMIC=2 / PT_INTERP=3 / PT_PHDR=6;interp(ldso)=ET_DYN=3,主程序动态 non-PIE=ET_EXEC=2。

## 设计 / 决策

- **抽 `load_elf_image(space, inode, ehdr, phdrs, phnum, base, out)`**(新 `kernel/proc/elf_load.{hpp,cpp}`):主程序(base=0,绝对 VA)和 interp(base=USER_INTERP_BASE,base+p_vaddr)共用同一条 PT_LOAD 映射路径(alloc/zero/read/map/VMA-record)。返回 `LoadedImage{entry, phdr_va, max_seg_end, has_load}`。
- **`load_interpreter(space, path, out_base, out_entry)`**:resolve+lookup interp inode → 读 ehdr + validate(收 ET_DYN)→ 读 phdrs → load_elf_image at USER_INTERP_BASE → `out_entry = USER_INTERP_BASE + interp.e_entry`(ET_DYN entry 是 base 相对)。把所有 ELF 机制收进 elf_load,execve 当纯编排。
- **execve**:clear_user_mappings 一次 → load_elf_image 主程序 → 扫 PT_INTERP 读路径(读 p_offset 处 p_filesz 字节,NUL 终止)→ delete[] phdrs → brk 用 main_img.max_seg_end → 若 has_interp 调 load_interpreter(改 entry)→ sigreturn 页 → aux 填 at_phdr=main_img.phdr_va / at_entry=主 e_entry / at_base=interp_base / has_interp。静态路径(无 PT_INTERP)走原路,entry=主 e_entry,at_base=0。
- **USER_INTERP_BASE = 0x10000000(256MB)**:放 heap 上限(64MB)与 mmap 区(4GB)间的空隙,与两者零碰撞;interp 用 `__ehdr_start` 自定位,任意 base 都行,固定取确定(ASLR interp base 留 follow-up)。
- **validate 收 ET_DYN**:interp 必需(它是 ET_DYN);顺带铺 PIE 主程序的路(留 follow-up)。test_fork_exec 的 5 个 validate 单测全用 ET_EXEC 不受影响,加 `test_valid_et_dyn`。

## 陷阱

- **Doxygen 注释里 `BadElf*/` 的 `*/` 提前关注释块** → 后面声明全被吞(load_interpreter "未声明")。措辞改成 `BadElfHeaders`。
- **`CINUX_BUILD_TESTS` 默认空(off)** → 新 worktree 首次 configure 必须传 `-DCINUX_BUILD_TESTS=ON`,否则 mini/test 子目录不加、mini_kernel_test 无规则、test-image 失败。主仓库 build/ cache 里存的是 ON。
- **并行 worktree 占 VNC 5900** → `run-kernel-test-all` 两 leg 的 `-vnc :0` 起不来 QEMU,wrapper 把 QEMU 启动失败(退出码 1)误判成 SUCCESS = **假绿**。解法:不动别人的 QEMU,直接用 wrapper 跑、`-vnc :1`(5901)。配置同构,仅 display 端口异。
- **test_host 8 项 "Not Run"**(initial_stack/tty/extable/net_*)是预存:它们不在 `ALL_HOST_TESTS` 里(test/CMakeLists 没动),test_host 不编其 exe → CTest 报 Not Run。与本次改动无关;编出来的 54 项全 PASS。

## 验证

- `timeout 40` 单 leg(wrapper + `-vnc :1`):**单核 968 passed, 0 failed**(+1 = test_valid_et_dyn,静态路径零回归)。
- **-smp 2 leg**:ALL TESTS PASSED + AP1 readback PASS(cr4/efer/lstar)。
- `test_host`:54/54 编出来的全 PASS(fork_exec/ext2_inode_ops/vfs_mount/elf_loader 等含相关面)。
- clang-format 跑过;big_kernel_test format 后重编通过。

## 范围栅栏

- 不自建 dynamic loader;不碰 GOT/PLT/DT_NEEDED(留 musl ldso)。
- ELF base ASLR / PIE 主程序留 follow-up(批1 只让 interp 的 ET_DYN 能载,主程序仍 non-PIE ET_EXEC)。
- 不碰 F10-M3 TTY / fork saga(已合 main)。

下一步:批2 musl 动态工具链(build-hello-dyn.sh + ext2 装 interp + qemu.cmake 穿 artifact)。
