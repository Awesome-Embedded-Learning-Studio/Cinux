# M6: Ext2 独立库 + Host 端测试

> 将 ext2 文件系统逻辑提取为可独立编译的库。
> 配合 F1 的 IBlockDevice 抽象，可在 Linux host 上独立测试 ext2 操作。

> **进度（2026-07-10 ✅ 收官 `feat/f6-m6-ext2-lib`，4 commit `ec320f2`→`c486595`）**：
> - **T1 ext2 解耦**：✅ B0a —— `git mv kernel/fs/ext2 → libs/ext2/`（13 文件），内部 include 归一化，外部 18 处改 `libs/ext2/`。**未建 STATIC lib**：执行时撞 codegen 矛盾（kernel build 要 `-mcmodel=kernel`，host build 不能；build/build-host 同一份顶层 CMakeLists 无法条件区分）→ 用户确认（2026-07-10）**双端直接编**（net_tcp 范式）：kernel `target_sources` 引 `libs/ext2/*.cpp` 继承 kernel codegen，host test `add_cinux_integration_test` 直接编 `libs/ext2/*.cpp` host codegen。物理 lib（libs/ext2/）达成，无 STATIC target。
> - **T2 host 块设备**：✅ —— 复用现成 `RAMBlockDevice`（header-only，吃 kmalloc/memcpy），未新写 FileBlockDevice。
> - **T3 host 测试**：✅ B1a/B1b/B2 —— `ext2_host_pal.cpp`（host PAL：kprintf/kvprintf/kpanic→vprintf/abort、kmalloc/kfree→aligned_alloc+zero、sink noop；**禁 link kernel 那份** kprintf.cpp/slab.cpp，吃 serial/inline-asm/PMM）+ `ext2_host_link`（冒烟：RAMBlockDevice→Ext2 ctor→mount→dtor，证 host 编链通，M6 关键关卡）+ `ext2_host`（真逻辑 ASAN：mount 镜像/readdir/多级 lookup/read/create/write/readback/unlink）+ `ext2_concurrent`（TSAN 并发 alloc/free）。预制 `test/data/ext2_test.img`（64KB，`mke2fs -d` 填 /etc/motd + /hello.txt，.gitignore `!test/data/ext2_test.img` 例外）。
> - **T4 CMake**：✅ —— `EXT2_HOST_LIB_SOURCES` 共享变量（ext2 9 .cpp + file.cpp + inode.cpp + host_spinlock + ext2_host_pal），`ext2_host`/`ext2_concurrent` 注入 `EXT2_TEST_IMG_PATH`。
> - **PAL 清单**：kprintf/kvprintf/kpanic（host PAL 自定义）/ kmalloc/kfree（aligned_alloc+zero）/ Spinlock（host_spinlock.cpp）/ inode_ref/unref（link file.cpp，devfs 范式）/ **hpet+backtrace+page_cache.invalidate_range**（`#ifndef CINUX_HOST_TEST` 守，易漏 HPET）/ lockdep_assert_held（LOCKDEP off 自动 no-op）/ string（libc）。
>
> **M6 ✅ 收官。** 详见 PLAN「✅ F6-M6 ext2 独立库」段 + [note](../../notes/2026-07-10-f6-m6-ext2-lib.md)。验证：`run-kernel-test-all` 两 leg 937/937 + `test_host` 66/66 + ASAN/TSAN host ext2 绿。
>
> **留续（非 M6 阻塞）**：
> - ⭐ **ext2 `block_buf_` SMP race**（M6 host TSAN 抓到的真 bug，F-DYN-COV QEMU forensics 漏）：`lookup_in_dir` 调 `read_block(block)`（无 dst 版）写共享 `Ext2::block_buf_` scratch（[ext2.hpp:120](../../../libs/ext2/ext2.hpp#L120) 自标 "NOT SMP-safe"），多 CPU 并发 lookup 互踩。F-DYN-COV 批3/批4 只修 inode_cache_/block_alloc_ 数据结构 race，没修 block_buf_ scratch 共享（lockdep 只看锁序，scratch-clobber 不触发 wild block 直到时序相关 corrupt）。修法：`lookup_in_dir` 改 `read_block(block, dst)` SMP-safe 版 + per-call KmBuf scratch（`Ext2FileOps::read/write` 已用范式）。属 ext2 SMP-correctness 修复，待续 —— 并发 lookup TSAN 测试已写好等修复后加回归（正是 M6 host-TSAN 解锁的回归守卫价值）。
> - **ext2 lib STATIC 化**（若未来需多项目复用）：当前物理在 libs/ext2/（B0a）+ 双端直接编；STATIC target 待 codegen 矛盾解（条件 codegen 或 build 目录分离）。

## 目标

ext2 作为 `libcinux-ext2` 独立库：
- 内核使用：通过 IBlockDevice 接口
- Host 测试：通过 RAMBlockDevice（文件模拟磁盘）

## 任务清单

### T1: ext2 代码解耦

**文件**: `kernel/fs/ext2*.hpp` / `.cpp`

当前 ext2 直接依赖内核头文件（kprintf、PMM、VMM 等）。解耦为：

```
libs/ext2/
├── include/
│   └── ext2/
│       ├── ext2_common.hpp    # 共享常量/类型
│       ├── ext2_superblock.hpp
│       ├── ext2_inode.hpp
│       ├── ext2_directory.hpp
│       ├── ext2_block.hpp
│       ├── ext2_init.hpp
│       └── ext2.hpp           # 统一 forward header
├── src/
│   ├── ext2_common.cpp
│   ├── ext2_superblock.cpp
│   ├── ext2_inode.cpp
│   ├── ext2_directory.cpp
│   ├── ext2_block.cpp
│   └── ext2_init.cpp
├── platform/
│   ├── kernel/                # 内核适配（kprintf → lib log）
│   │   └── ext2_platform.cpp
│   └── host/                  # Linux mock（printf 替代）
│       └── ext2_platform.cpp
└── test/
    ├── test_ext2.cpp           # Host 端单元测试
    └── test_disk.img           # 预制的 ext2 测试镜像
```

- [ ] 提取 ext2 核心逻辑为平台无关代码
- [ ] 平台抽象层：日志、内存分配、assert
- [ ] 内核适配层转发到 kprintf/kmalloc
- [ ] Host 适配层转发到 printf/malloc

### T2: RAMBlockDevice（Host 端）

```cpp
// Host 端块设备实现（文件模拟磁盘）
class FileBlockDevice : public IBlockDevice {
public:
    FileBlockDevice(const char* path);  // 打开磁盘镜像文件
    bool read_blocks(uint64_t block, uint64_t count, void* buf) override;
    bool write_blocks(uint64_t block, uint64_t count, const void* buf) override;
    uint64_t block_count() const override;
    uint64_t block_size() const override { return 1024; }
};
```

- [ ] FileBlockDevice 实现（使用 fopen/fread/fwrite）
- [ ] 支持创建指定大小的空 ext2 镜像

### T3: Host 端测试

- [ ] 挂载预制的 ext2 镜像
- [ ] 读取超级块 + 验证 magic
- [ ] 根目录列举文件
- [ ] 读取文件内容
- [ ] 创建文件 + 写入 + 重新读取验证

### T4: CMake 集成

- [ ] libs/ext2/CMakeLists.txt — 独立库构建
- [ ] 内核通过 target_link_libraries 引入
- [ ] Host 测试通过 CMake test 注册

## 产出物

- [ ] `libs/ext2/` 独立目录结构
- [ ] 平台抽象层
- [ ] FileBlockDevice host 实现
- [ ] Host 端 ext2 单元测试
- [ ] 内核通过 IBlockDevice 使用 ext2
