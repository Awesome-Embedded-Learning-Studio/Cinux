# F-INFRA I-6 — 64 位 .gdbinit + decode-trace.sh

> 日期 2026-06-19 · F-INFRA Tier1 批 I-6 · 分支 `feat/finfra`

## 背景
R9/G5：`scripts/.gdbinit` 是 16 位实模式 MBR 时代遗留（`break *0x7c00`、16 位 AX/BX/CX/DX dump、`dump-mbr`），对 64 位内核 panic/页错调试无用；`qemu.cmake` 的 `run-gdb` 引用陈旧路径 `build/kernel.elf`（实际在 `build/kernel/big/big_kernel`）；host addr2line 符号化全靠手敲。

## 目标
1. .gdbinit 重写为 64 位长模式：正确符号加载、64 位寄存器 dump、panic/#PF 断点。
2. decode-trace.sh：一键 addr2line，**demangled**（补 I-5 内核内 mangled 名的不足）。
3. 修 run-gdb 陈旧路径。

## 设计/决策（验证器 R9 修正）
- **符号路径** `build/kernel/big/big_kernel`（[qemu.cmake](../../cmake/qemu.cmake) `BIG_KERNEL_BIN`），非 `build/kernel/big_kernel`。
- **不写 0x1000000 偏移**：那是物理 LMA；内核跑在 higher-half VMA `0xFFFFFFFF81000000`。big_kernel 是真 ELF，PT_LOAD 自带 VMA，GDB 自推——实测 `file build/kernel/big/big_kernel` 后 `info functions kernel_main` 正确解析 @0xffffffff8100004e。传 0x1000000 反而会让符号映射到低地址、断点全失效。
- **`set architecture i386:x86-64`**（对齐 `.vscode/launch.json` 既有约定，非 i686）。
- `launch_qemu_debug.sh` 已存在（`make run-debug` 已接 `-s -S`），是修不是建。
- `run-gdb` 改用 `gdb -x scripts/.gdbinit` + `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}`（.gdbinit 用项目根相对路径）。

## decode-trace.sh（G5）
- `addr2line -e <elf> -f -C`：`-C` **demangle**（I-5 内核内是 mangled，本脚本给 demangled 源码位置）。
- 三模式：地址 argv、`[elf] addrs`、stdin 抽取 `0x...`。
- 实测：`0xffffffff810083bc → test_task_state::test_state_transitions()`、`0xffffffff8100004e → kernel_main`。

## 陷阱
- **行号 `??:?`**：当前 build（默认空 CMAKE_BUILD_TYPE，无 `-g`）只有 symtab 无 DWARF line 表 → 函数名 ✓ 但行号 ??:?。**`-DCMAKE_BUILD_TYPE=Debug` 构建**（带 `-g`）即有完整 file:line。非脚本缺陷。
- **`set -u` + 空 `$1`**：decode-trace.sh 初版 `ELF="${1:-}"` 在 case 判断前吞掉 $1（地址被当 ELF）、空参未绑定。重写参数解析：先判 `$#`/首参是否 `0x*` 再定 ELF。

## 验证
- `gdb --batch file build/kernel/big/big_kernel; info functions kernel_main` → 解析正确。
- decode-trace.sh 三模式测通（demangled 名正确）。
- 无内核代码改动，run-kernel-test 不受影响（基线 840/0 保持）。

## 文件
- 改：`scripts/.gdbinit`（64 位重写）、`cmake/qemu.cmake`（run-gdb 用 .gdbinit + 项目根 + 修路径）。
- 新：`scripts/decode-trace.sh`。
