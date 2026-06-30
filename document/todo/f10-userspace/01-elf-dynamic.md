# M2: ELF 动态链接（musl ldso 路线，F10 重构 2026-06-30）

> **方向决策（用户 2026-06-30）**：按需建动态链接，对齐 Linux 不重造。
> **不自建动态 loader**——用 musl 自带的 ld-musl(ldso)做符号解析 / GOT 填充 / DT_NEEDED。
> kernel 只做最小三件事：解析主程序 PT_INTERP → 加载 interp(ldso)ELF → 喂对 auxv。
> libc 解耦：PT_INTERP 是 musl/glibc 切换缝，kernel **不写死 libc 名**（musl `/lib/ld-musl-x86_64.so.1`、glibc `/lib64/ld-linux-x86-64.so.2` 同一套机制）。
> ELF base ASLR / PIE 主程序是 follow-up，先 non-PIE 动态跑通。
>
> 本文取代旧版「自建 ld-cinux + 内核做 GOT/PLT 重定位」规划（造轮子无意义）。
> 真实进度源：`document/ai/PLAN.md`「🔄 F10-M2」批表。本文件是 M2 的契约 + 调研实证。

## 目标

musl-gcc / gcc 动态链接（PT_INTERP=/lib/ld-musl-x86_64.so.1）编的 hello world，经 execve
+ 内核 interp 加载 + musl ldso 用户态重定位，在 Cinux QEMU 上跑起来，`write`/`printf` 真输出。
即：内核 execve 识别动态 ELF（PT_INTERP）→ 加载 interp + 喂对 auxv，剩下交给 musl ldso。

## 调研实证（决定打法，已拉 musl-1.2.5 源码核实，不猜）

- **interp 路径串** = `/lib/ld-musl-x86_64.so.1`（musl Makefile `LDSO_PATHNAME = $(syslibdir)/ld-musl-$(ARCH)$(SUBARCH).so.1`；x86_64 + 空 SUBARCH）。`make install` 产 `lib/libc.so`(ldso 本体)+ 装 symlink。
- **shared 默认开**（configure `--disable-shared [enabled]`）→ 现有 `tools/musl/build-musl.sh` 已自带 `libc.so`，动态 hello 只需新写链接脚本，不改 musl 构建。
- **ldso(ET_DYN)靠 `__ehdr_start` 定位自身**（`ldso/dynlink.c`）→ kernel 只需把它在某个 base 连续映上去；AT_BASE 给了更稳。
- **ldso 读的 auxv 契约**：`AT_PHDR/PHNUM/PHENT`(主程序 phdr VA —— ldso 用 `AT_PHDR - PT_PHDR.p_vaddr` 算主程序 base)、`AT_ENTRY`(主程序 entry —— 重定位完 `CRTJUMP(aux[AT_ENTRY])` 跳过去)、`AT_BASE`(ldso base)、`AT_PAGESZ`、`AT_UID/EUID/GID/EGID/AT_SECURE`、`AT_HWCAP`、`AT_EXECFN`、`AT_RANDOM`，vDSO 可选。
- **ELF 常量**：PT_DYNAMIC=2 / PT_INTERP=3 / PT_PHDR=6（musl `include/elf.h`）。interp(ldso)=ET_DYN=3；主程序动态 non-PIE=ET_EXEC=2。
- **现有静态加载基线**：`kernel/proc/execve.cpp` + `elf_loader.cpp` 加载静态 ELF（inode->ops->read 读 ELF header + 段映射 + auxv 已压 F10-M1 批3）。动态链接同路径，加 PT_INTERP 解析 + interp 加载。

## kernel 做什么 / 不做什么（契约边界）

**做**：
1. 主程序 phdr 扫到 PT_INTERP → 读 interp 路径串（从主 ELF 文件 PT_INTERP.p_offset 处读 p_filesz 字节）。
2. vfs_resolve + lookup interp inode → 当 ET_DYN 映到 `USER_INTERP_BASE`（base + p_vaddr，页对齐映射 + 读 filesz + 记 VMA，复用主程序的 PT_LOAD 映射逻辑）。
3. entry = `USER_INTERP_BASE + interp.e_entry`；auxv 喂 `AT_BASE=USER_INTERP_BASE`、`AT_ENTRY=主程序 e_entry`、`AT_PHDR=主程序 phdr VA`（现状已算）。

**不做**（全由 musl ldso 在用户态干）：GOT/PLT 重定位、R_X86_64_GLOB_DAT/JUMP_SLOT、DT_NEEDED 共享库搜索、符号查找、.init_array。kernel 不实现 dynamic loader。

**静态路径**（无 PT_INTERP）：行为完全不变（AT_BASE=0、entry=主程序 e_entry）。

## 批分解（每批≈一 commit，门 `timeout 120 run-kernel-test-all` 两 leg 绿）

| 批 | 范围 | 完成门 |
|----|------|--------|
| 0 | 立项 docs（PLAN/ROADMAP/本文件）| docs-only |
| 1 | kernel 核心：ELF 常量(ET_DYN/PT_INTERP/PT_DYNAMIC/PT_PHDR) + validate 收 ET_EXEC∨ET_DYN(+ET_DYN 单测) + USER_INTERP_BASE + ElfAuxInfo{at_base,has_interp} + execve 抽 load_elf_image helper(主+interp 共用) + 扫 PT_INTERP→加载 interp→entry=interp 入口 + enter_loaded_program 发 AT_BASE | run-kernel-test-all(关 smoke)两 leg 绿(零回归静态)+ ET_DYN 单测 + 全量 host |
| 2 | 工具链：build-hello-dyn.sh(动态 musl hello) + README + create_ext2_disk.sh 装 interp + qemu.cmake 穿 artifact | host 跑 hello-dyn 输出对 + readelf 段/interp 对 |
| 3 | 端到端 dyn smoke + notes + ROADMAP/PLAN 收官 | run-kernel-test-all(dyn smoke ON)两 leg 绿 + 动态 hello 真输出 + 默认 OFF 仍绿 |

## 风险

- **批1 抽 helper 碰启动核心路径**：保静态路径行为不变（无 PT_INTERP 走原路），用既有 musl 静态 smoke + ET_DYN 单测兜回归。
- **interp 是 ET_DYN**：映到 USER_INTERP_BASE(base + p_vaddr)；validate 放宽收 ET_DYN 为 interp 必需（顺带铺 PIE 主程序的路，留 follow-up）。
- **interp 必须装进 ext2 的 `/lib/ld-musl-x86_64.so.1`**（PT_INTERP 指定路径），否则 execve 找不到。
- **smoke 默认 ON + 本地无 `build/musl/hello-dyn` → 挂死**，本地验证关 `-DCINUX_MUSL_DYN_SMOKE=OFF`。
- **musl-gcc wrapper 在 GCC≥14 坏**（README gotcha #2），hello-dyn 也手动 `-nostdlib` 链。

## 产出物

- [ ] kernel 侧 PT_INTERP 解析 + interp(ET_DYN)加载 + auxv(AT_BASE/AT_ENTRY/AT_PHDR)
- [ ] validate_elf_header 收 ET_DYN(+ 单测)
- [ ] musl 动态 hello 工具链（build-hello-dyn.sh）+ ext2 装 interp
- [ ] 动态 hello 在 Cinux QEMU 跑通（ring-3 dyn smoke）
- [ ] M2 完成后演进：ELF base ASLR / PIE 主程序（follow-up）；glibc 动态二进制（PT_INTERP 切换缝天然支持，按需验）
