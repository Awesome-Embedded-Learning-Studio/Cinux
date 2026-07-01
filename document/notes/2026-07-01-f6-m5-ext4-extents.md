# F6-M5 ext4 extents（读路径）

> 2026-07-01 · 分支 `feat/f6-m5-ext4`（worktree `.claude/worktrees/wt-ext4`，从干净 main `5c03f85`）。
> 两 leg 1066/0 + host 单测绿。

## 背景

ext2 驱动原本只认 classic indirect block pointer：`i_block[0..11]` direct + `i_block[12]` single-indirect + `i_block[13]` double-indirect（见 `ext2_common.cpp` `Ext2FileOps::read`、`ext2_inode.cpp` `get_or_alloc_block`）。ext4 卷上，文件的 `i_block[0..14]`（60 字节）被重解释成一棵 **extent tree**（inode 置 `EXT4_EXTENTS_FL=0x80000`），不再是块指针数组。驱动对这种 inode 读出来的是 extent header 的魔数当块号，整文件读错。

## 目标

**只做读路径**：让 ext2 驱动能读 extent-mapped inode。范围栅栏（留 follow-up）：

- depth-0 leaf extent（一个 header + N 个 `ext4_extent` 叶子）—— 本批
- depth>0 index node（大文件/碎片化）—— 留续
- extent **写**路径（内核新建文件仍走 indirect block，读 ext4 卷无碍）—— 留续
- journal —— 只读驱动忽略

## 设计

**新增 `kernel/fs/ext2/ext2_extent.{hpp,cpp}`**（独立文件——`ext2_common.cpp` 已 483 行，再加 resolver 破 500 行软上限，CODING-TASTE §7b）：

- `extent_lookup_block(disk, file_block, out_block) -> ExtentLookupResult{Mapped, Hole, Unsupported}`：纯函数，从 `disk.i_block`（60 字节）读 extent tree root，校验 `eh_magic==0xF30A`、拒 `eh_depth>0`，遍历 leaf extent 算 `phys = (ee_start_hi<<32|ee_start_lo) + (file_block - ee_block)`；`ee_len>32768` 的 uninitialized extent 与 extent 间空洞当 Hole（零填）。
- `inode_read_block(disk, logical) -> uint32_t`：读侧块解析，extent tree 走 `extent_lookup_block`、否则 `i_block[logical]`（direct 区）。目录扫描共用。

**接线**：

- `Ext2FileOps::read`（`ext2_common.cpp`）每个块循环顶加 extent 分支：`inode_has_extent_tree(disk)` → resolver（Mapped→disk_block，Hole→0 复用既有零填，Unsupported→break）；否则原 direct/indirect 路径不动。
- `lookup_in_dir`（`ext2_init.cpp`）+ `readdir`（`ext2_common.cpp`）的 `i_block[b]` 改 `inode_read_block(disk, b)` —— **目录在 ext4 上也是 extent-mapped**（见陷阱 2）。
- `Ext2::has_ext4_extents_feature()` 公开 accessor（`sb_.s_feature_incompat & 0x40`）给测试断言"挂的是 ext4 extents 卷"。

**类型**（`ext2_types.hpp` 追加）：`EXT4_FEATURE_INCOMPAT_EXTENTS=0x40`、`EXT4_EXTENTS_FL=0x80000`、`EXT4_EXTENT_MAGIC=0xF30A`、`EXT4_EXTENT_INIT_LEN_MAX=32768` + packed `Ext4ExtentHeader`(12B)/`Ext4Extent`(12B) + static_assert。

## 决策

1. **独立 ext4 测试盘挂 AHCI port 2**（不把 `ext2.img` 改 ext4）。隔离 50+ 既有 ext2 测试 + ring-3 musl smoke（它们仍用纯 ext2 rev0 盘）；专盘专测，对齐"机制验证 test"方法论。AHCI 最多 32 port，既有测试只硬编码 port 0/1，port 2 空闲。
2. **ext4 镜像强制 32 字节 group descriptor**（`mkfs.ext4 -O extents,^64bit,^metadata_csum`）。驱动 BGDT 读按 `sizeof(Ext2BlockGroupDescriptor)=32` 定步长、**不看 `s_desc_size`**（`ext2_init.cpp` mount）；stock ext4 默认 64 字节描述符会让 `bg_inode_table` 读错、mount 失败。脚本用 `dumpe2fs` 断言 features，错了大声失败。
3. **extent resolver 独立文件**（见上，500 行约束）。
4. **depth>0 直接 bail**（返 Unsupported），绝不静默读错；TODO 标 follow-up。

## 陷阱

1. **BGDT 步长 vs `s_desc_size`**：见决策 2。`^64bit,^metadata_csum` 后 `dumpe2fs` 不打印 `Desc size`（=0，经典 32 字节），mount 才能找到 inode table。
2. **目录也 extent-mapped**（本批头号坑）：初版只给 `Ext2FileOps::read`（普通文件）加了 extent 分支，mount 测试过了，但 `lookup("big.bin")` 全返 nullptr。debugfs 确认 **inode 2（根目录）Flags=0x80000** —— ext4 目录同样 extent-mapped，而 `lookup_in_dir`/`readdir` 直接扫 `i_block[]` 没走 resolver，读到 extent header 魔数当块号 → 目录项全找不到。修法：抽出 `inode_read_block()` 共用读侧解析，目录扫描两处都改用它。修完后 1 MiB 大文件 byte-exact 读过。
3. **测试字节数 off-by-one**：`printf 'ext4 extents small file\n'` 是 **24** 字节（不是 25），`strcmp` 期望串起初漏了 `\n`、`n==25` 又错算。读路径本身正确（1 MiB 全 pattern 验过），纯测试期望 bug。

## 机制测

`kernel/test/test_ext4_extents.cpp`（挂 port 2 → Ext2 mount ext4 卷）：

1. `has_ext4_extents_feature()` 真（mount 探到 ext4 extents 卷）。
2. `/big.bin` inode `i_flags & EXT4_EXTENTS_FL` 真（确认真走 extent 路径，而非误落 indirect）。
3. `/big.bin`（1 MiB，单 depth-0 leaf extent，逻辑块 0..1023→物理 1138..2161）分块读到 EOF，逐字节验 `byte[i]==i&0xFF` + 总长 == 1048576。
4. 跨块边界读（offset 1020 读 8 字节）pattern 正确。
5. `/small.txt`（单块 extent）读回 `"ext4 extents small file\n"`（24 字节）。

镜像由 `scripts/create_ext4_disk.sh` 产（`mkfs.ext4 -t ext4 -b 1024 -O extents,^64bit,^metadata_csum -N 128` + `debugfs -w` 写 big.bin/small.txt + dumpe2fs 断言）。

## 验证

```
cmake -B build -DCINUX_MUSL_DYN_SMOKE=OFF -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cmake --build build --target run-kernel-test -j$(nproc)      # 单核：1066/0
cmake --build build --target run-kernel-test-smp -j$(nproc)  # -smp 2：1066/0
cmake --build build --target test_host -j$(nproc)            # host 单测：绿
```

（本机 `/dev/kvm` 无权限 → 配置时 `CINUX_NO_KVM=1` 走 TCG；两条 leg 分开跑因 TCG 下双 leg 超 600s 工具上限。功能等价 `run-kernel-test-all`。）

## Follow-up

- depth>0 index node（很大/碎片化文件）。
- extent 写路径（新建文件仍 indirect；读 ext4 卷无碍）。
- `s_desc_size`-aware BGDT 读（才能挂 stock 64-bit ext4）。
- ext4 symlink 读（长 symlink 是 extent-mapped 数据块；`readlink` 现仍读 `i_block[0]`）。
- journal（只读驱动忽略即可）。
