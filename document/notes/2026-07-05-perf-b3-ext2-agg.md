# B3a — ext2 read 聚合连续 block(+ agg>1 SIGILL 真凶定位)

> 批次:perf-B3a · 分支 `feat/perf-b1-stats-profiling` · commit `aaf5361`(接 B2.5 `123a6c0`)
> 关联 memory:`gcc-compile-stutter-perf`(B1→B2→B2.5→B3a)、`b3-ext2-agg-sigill-handoff`(排查 handoff,已解决)

## 背景

B2.5 计时(commit `123a6c0`)抓到 gcc 编译的真凶:ext2 read I/O 占 72%(gcc 编译 ~8s 里 5.8s 在 ext2 read)。根因是 1 page = 4 block = **4 个独立 AHCI 命令**(`Ext2FileOps::read` 逐 block `read_block`,buildroot rootfs `bs=1024`)。B3a 目标:聚合连续 block 为 1 次 DMA,预期减少 AHCI 命令开销。

前任 AI 在工作树留了 agg 基建,但 agg>1 必发 SIGILL(rip=0x1000377e,ldso .text),数据看起来对(.text dst 逐字节 == host),排除了 AHCI count=8、ldso 时序等嫌疑后卡住,留 handoff 给本会话。

## SIGILL 真凶(深查定位)

经过多轮仪器排查(GP-REGS / RIP-LEAF / PTE per-level walk / VMA list / PF-BSS / ANON-BSS),定位真凶链:

1. **rip=0x1000377e 在 ldso .text**(RIP-LEAF 字节 `48 89 50 08 e9 54 ff ff` == host ldso 0x377e,ldso_base=0x10000000)。
2. `mov 0x393db(%rip),%rax` 读 `_rtld_global+0xb50`,目标 va = `0x10000000 + 0x3cb50` = **`0x1003cb50`**(ldso 自己的 .bss,应零填)。
3. VMA list(`IVMAStore::first/next` 遍历)确认 `0x1003cb50` 在 ldso RW PT_LOAD 的 straddle page `[0x1003c000, 0x1003e000)`,**匿名**(eager 零填)。ANON-BSS 0 个 → 该页未走 demand PF,是 elf_load eager map 的。
4. BSS-LEAF:`phys 0x1D9F000 off 0xb50 = 73 64 74 00 a7 55 02 00`(`0x000255A700746473`,**非零**)。匿名页应零填,却读到垃圾 → rax 非规范 → `mov %rdx,0x8(%rax)` 写非规范 → **#GP err=0 → SIGILL**。
5. **越界源头**:`elf_load.cpp` eager 填 straddle page 时,`read(inode, file_off=0x3b000, dst, copy_len=0x84c)` 只该读 **0x84c 字节**(file tail)。但 agg>1 path 的 `read_disk_range(disk_block, agg, dst + total_read)` 写 **agg\*bs = 2 KB** 直接到 dst —— 多写的 `dst[0x84c..0x800]` 覆盖了 straddle page 的零填区(`.bss` 半页),`dst[0xb50]` 被写成 `file[0x3b000 + 0xb50] = file[0x3bb50]` 的字节。

**根因一句话**:agg>1 path 违反 `read` 契约 —— read 只保证写 `count` 字节,但 coalesced DMA 写了整 block 倍数,越界覆盖调用者缓冲区之外的相邻数据(ELF straddle page 的零填 BSS 是典型受害者)。

### 排查中的两个坑(留作方法论)

- **RIP-LEAF 指针算错**:walk 得 leaf phys 后,指针写成 `phys + (kRipVa & 0xfff)`(加了 rip 页内 offset),又索引 `rip[0x76e]`,结果读了 `phys[0xeedc]`(跨页垃圾),一度误判 rip 不在 ldso。修:用页基址 `phys`,索引 `[0x77e]`。
- **地址位数算错**:ldso base `0x10000000` + `0x3cb50` = `0x1003cb50`(8 位 hex),误写 `0x10003cb50`(9 位,4 GB+ 区域),导致 PF filter 抓错页、PTE walk 落到未映射区,产生一堆误导性的"NOT PRESENT"。

## 修复

`Ext2FileOps::read` 的 agg>1 path 不再直接把 DMA 落到 `dst`,改成:

```cpp
if (agg > 1) {
    // DMA agg contiguous blocks into block_buf_ (scratch), then copy ONLY the
    // bytes the caller asked for. count may not be a block multiple (ELF
    // straddle pages, short reads); writing agg*bs to dst would overrun and
    // clobber adjacent data.
    ext2_.read_disk_range(disk_block, agg, ext2_.block_buf());
    uint64_t take = min(agg * bs, to_read - total_read);
    memcpy(dst + total_read, ext2_.block_buf(), take);
    total_read += take;
}
```

附带重构:提取 `resolve_disk_block_(file_block → disk_block)` helper(read 主循环 + agg 探测复用,原 inline indirect/extent 解析挪到私有方法);新增 `Ext2::read_disk_range(start_disk_block, block_count, buf)`(连续块一次 DMA)。

B3b readahead 基建(前手留的 `if (false)` 框架 + VMA `ra_state`)撤回原,B3b 独立重做。

## 验证

- `run-buildroot-usability`:gcc-compile-run + gpp-compile-run **PASS**(原 agg>1 必 SIGILL)。
- `run-kernel-test-all` 两 leg(单核 + -smp 2):**885 / 0 failed**。
- `cmake --build build` 全量 + `test_host`:**PASS**(改公共 ext2 read)。
- **I/O 加速**:`[MEM] I/O` 行 ext2 read 时间 **12799 ms → 9886 ms(-25%,~3 s)**(reads 数不变 = `Ext2FileOps::read` 调用次数;时间减来自 AHCI 命令聚合)。总 gcc+g++ 编译 22.3s → 19.3s。

减幅 25% 而非预期 4×,因为 QEMU AHCI 仿真里命令开销只占 I/O 一部分(传输也占)。真硬件 / 后续 B3b readahead(预取减 demand PF)会进一步加速。

## 教训

- **read 契约是写 `count` 字节**:任何 coalesced/聚合 I/O 写整 block 倍数时,必须落到 scratch 再 bounded memcpy,不能直接写调用者 buf。
- **ELF straddle page 是越界高危区**:同一页里 file tail + 零填 BSS 共存,任何多写都脏 BSS。elf_load 的 eager straddle read 是触发点。
- **仪器指针运算要画图**:walk 得 leaf phys 后,再索引页内 offset 时,基地址不要再加 rip offset(否则跨页)。地址位数手算后用十六进制对齐核对(linux base 0x10000000 + 0x3cb50 ≠ 0x10003cb50)。
