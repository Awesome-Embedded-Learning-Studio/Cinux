# F10-M2 follow-up — ext2 double-indirect 块映射 + 撤 4096 workaround

> 2026-06-30。F10-M2 收官时登记的 follow-up（见 [2026-06-30-f10-m2-b3-dyn-smoke.md](2026-06-30-f10-m2-b3-dyn-smoke.md)「关键坑」+ PLAN「F10-M2」段 follow-up 列表）的真修：ext2 块映射只处理 direct + single-indirect，>268 KB(1024 块)/>4 MB(4096 块)的文件读会截断；F10-M2 批3 把 QEMU ext2 image 改 4096-byte 块规避（822 KB musl ldso 落进 single-indirect 4 MB 上限）。本弧补 double-indirect(i_block[13]) 的读+写，并撤掉那个 workaround。分支 `worktree-ext2-indirect`（从干净 main `c0188cd`）。

## 背景 / 目标

F10-M2 端到端跑 musl 动态 hello 时撞上的铁证：interp（`/lib/ld-musl-x86_64.so.1`，822 KB）读到 offset 274432 处失败 —— `274432 = 268 × 1024`，正是 1024-byte 块下 **direct(12) + single-indirect(256) 的双重间接起点**。`ext2_common.cpp` 的 read 在该处直接 `break`（截断），`get_or_alloc_block` 也没分支。当时为了不动 kernel，把盘改成 4096-byte 块（single-indirect 上限 ≈4 MB）绕过。

本弧真修：读路径 + 写路径都补 double-indirect 三层解析，然后把盘改回 1024-byte 块（ext2 默认），让 double-indirect 真正有人走。triple-indirect(slot 14) 不做 —— 那是 >16 GB 文件的事，hobby OS 用不到。

## 范围栅栏

改动严格圈在两函数内（零文件冲突）：
- [ext2_inode.cpp](../../kernel/fs/ext2_inode.cpp) `Ext2::get_or_alloc_block` —— 加 i_block[13] 三层 lazy-alloc 分支。
- [ext2_common.cpp](../../kernel/fs/ext2_common.cpp) `Ext2FileOps::read` —— 加 double-indirect 纯读 else-if；`write` —— 放开 `file_block` 上限（从 `> EXT2_DIRECT_BLOCKS` 扩到 double-indirect 上限）。

不碰 `InodeOps` / PageCache / 公共接口签名。

## 设计 / 决策

- **三层索引算术**（与 ext2 规范一致）：double-indirect 区间内
  `offset = file_block - (DIRECT + ptrs_per_block)`、`idx1 = offset / ptrs_per_block`（外层块里哪个 single-indirect 指针）、`idx2 = offset % ptrs_per_block`（那个 single-indirect 块里哪个 data 指针）。`ptrs_per_block = block_size/4`，`di_limit = DIRECT + ptrs + ptrs²` 用 uint64 算，4096 块下 `1024²` 也不溢出。
- **scratch buffer 顺序（头号 GOTCHA）**：`block_buf_` 是单块 4 KB 缓冲，每次 `write_block()` 都把它覆盖。所以 double 写的每一层：alloc + 写盘下层后，必须**重新 `read_block()` 上层**再改它的指针（仿现有 single-indirect 的二次 read-modify-write，[ext2_inode.cpp:354-361](../../kernel/fs/ext2_inode.cpp)）。三层逐层下钻、逐层落盘。错则毁 inode 元数据。
- **读路径镜像写路径**：纯读三层，`disk_block == 0`（hole）落到公共零填路径；外/中层块缺（`i_block[13]==0` 或 child==0）才 `break`（与 single-indirect 一致语义）。
- **host 单测是算法真门**：现有没有 in-kernel 测试锻炼 indirect（shell 17 KB、motd/hello.txt 都 direct），CI 也不跑 musl smoke。所以 [test_ext2_ops.cpp](../../test/unit/test_ext2_ops.cpp) 的 `host_file_write`/`host_file_read` 补 single+double（镜像 kernel 算法，抽 `host_resolve_data_block(disk, inode, file_block, alloc)` 共用 resolver），加一条 >single-indirect-ceiling 的 write→read round-trip 用例（4 group = 512 block 模拟盘，写 276 块 = 268 直/单 + 8 双）。host sim 直访 `data_blocks[]`，没有 kernel 的 scratch-dance，但**三层算术与盘上布局与 kernel 完全一致** —— 这正是它能守算法不变量的价值。
- **dyn smoke 真内核覆盖**（补，见下验证段）：host 单测守算法之外，建 musl sysroot 开 `CINUX_MUSL_DYN_SMOKE` —— 822 KB ldso 装盘后 execve 读它真走 i_block[13]，5/5 PASS 实证 double-indirect 在 QEMU 真内核工作。**double-indirect 现在是 host 单测（算法）+ dyn smoke（真内核）双覆盖**，不再只是 opportunistic。
- **撤 workaround**：`create_ext2_disk.sh` `BLOCK_SIZE 4096→1024`，注释从「workaround：double-indirect 截断」改成「double-indirect 已支持，1024 块是 ext2 默认」。所有现存盘上文件（shell 17 KB / motd / hello.txt）远低于 268 KB single-indirect 上限，纯 direct，零行为变；822 KB ldso（仅本地 musl sysroot 装盘时）现走 double-indirect —— 那是 opportunistic 端到端验证。

## 关键坑

- **`-Werror=shadow`（kernel 构建）**：double read 分支里 local `offset` 遮蔽了 `read()` 的形参 `offset`；重命名 `di_offset`。另外 clangd 先报了一个 `indirect` 与 single-indirect 兄弟分支同名 shadow（IDE 当 Error）—— 兄弟 else-if 作用域本不冲突，但为干净把 double 分支的 `indirect` 改名 `child_ptrs`。
- **write 旧的 file_block 门太紧**：原 `if (file_block > EXT2_DIRECT_BLOCKS) break;` 只允许 block 0..12（12 direct + 单 indirect 第 0 槽），single-indirect 256 个槽基本没用上。放开到 `>= max_file_block`(double 上限) 才让写真正能长进 single/double 区。
- **worktree VNC 5900 撞车**：`run-kernel-test-all` 用 `-vnc :0`，另一 worktree 的 runaway QEMU 占着端口 → QEMU 起不来 → 测试「假绿」（exit 0 但 0 测试跑）。诊断法：日志里找 `qemu-system-x86_64: -vnc :0: Failed to find an available port`。等端口空了重跑才真验。`make run` 用 `-vnc :0` 同理（memory 已记并行 worktree 用 `-vnc :1`）。
- **`test_host` umbrella 排序怪**：`cmake --build build --target test_host` 会在部分 test 二进制还没编完时跑 ctest → 9 个测试「Not Run」（tty/pty/extable/net_*/initial_stack），逐个 `cmake --build build --target test_<name>` 又全 OK。不是真失败，单独编完再 `ctest` 即 65/65 全绿。
- **Cinux-GUI submodule 拉不下来**：本环境无 github 网，`git submodule update --init` 只成了 Cinux-Base（ErrorOr 必需），Cinux-GUI 失败 → 配 `-DCINUX_GUI=OFF` 跑（ext2 不依赖 GUI）。test 计数 762/leg（GUI OFF，关了一批 GUI/cgui 测试），vs GUI ON 的 ~986；回归比对以同配置为准。

## 验证

- `cmake -B build -DCINUX_MUSL_HELLO_SMOKE=OFF -DCINUX_MUSL_DYN_SMOKE=OFF -DCINUX_BUILD_TESTS=ON -DCINUX_GUI=OFF`。
- **run-kernel-test-all 两 leg（4096→1024 撤 workaround 后）**：单核 + -smp 2 各 **762 passed, 0 failed**，`ALL TESTS PASSED`。零回归。
- **host 单测**：`test_ext2_ops` **30/0**（+1 `file_indirect: write into double-indirect range round-trips`：全量 round-trip + 三层 spot-read direct/single/double + 断言 i_block[12]/[13] 头指针已置）；全 host `ctest` **65/65**（100%）。
- **`make run` boot 冒烟**（timeout 40）：生产 kernel 起，`[EXT2] block_size=1024 blocks=8192 groups=1`、`[VFS] ext2 mounted at /`、`[DEVFS] mounted at /dev (4 nodes)`、`/bin/sh` execve + 跳用户态、`cinux>` shell 提示符，零 panic（仅 timeout 信号杀进程）。
- **musl dyn smoke（double-indirect 真内核验证）**：建 musl sysroot（`tools/musl/build-musl.sh`，musl 1.2.5 在本机 gcc 16.1.1 编通）+ `build-hello-dyn.sh`，`cmake -DCINUX_MUSL_DYN_SMOKE=ON`。盘装 `/lib/ld-musl-x86_64.so.1`（**822368 B = 803 个 1024-byte 块**，远超 single-indirect 上限 268）→ execve("/hello-dyn") 内核读 ldso 的 PT_LOAD segment，offset 274432(268×1024)+ **全部走新写的 i_block[13] double-indirect**。run-kernel-test-all 两 leg（单核 + -smp 2）各 `[F10-M2] smoke: hello-dyn 5/5 iters PASS -> PASS` + 串口 5× `Hello from musl on CinuxOS!`；**无** `[ELF] segment read failed at offset 274432`（那是 double-indirect 坏的现象，F10-M2 没修前就是它）。这是 double-indirect 在 QEMU 真内核被走到且工作正常的铁证 —— 不再只靠 host 单测。

## 收官

5 commit（批0 docs `7f27c32` / 批1 写路径 `8683efd` / 批2 读路径+门 `fd6008f` / 批3 host 单测 `cff0cb1` / 批4 撤 workaround + 本 note）+ 批5 docs（dyn smoke 真内核覆盖实证，本段更新）。F10-M2 follow-up「ext2 double-indirect/triple-indirect 缺失」关闭；triple（slot 14）显式不做，留作 >16 GB 文件的远期项。**覆盖**：host 单测（算法 round-trip）+ musl dyn smoke（真内核读 822 KB ldso 走 i_block[13]）+ run-kernel-test-all 两 leg 762/0 + make run boot 冒烟。分支 `worktree-ext2-indirect` 待 push。**push/PR 归用户**。
