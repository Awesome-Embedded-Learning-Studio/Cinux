# M3: DevFS

> /dev 设备文件系统。统一字符/块设备访问接口。
> 设备节点自动创建，用户程序通过 open/read/write 访问硬件。

> ⚠️ **范围栅栏（F6-M3 实施批,2026-06-30 立项；三路并行之一,另两路 F7-M4 UDP / F10-M2 动态链接）**
>
> **本批做**:`DevFs`（`FileSystem` 子类,内存型虚拟 FS,无 ext2 后端)+ device inode（`InodeOps` 子类:`NullDevOps`/`ZeroDevOps`/`ConsoleDevOps`/`DevDirOps`,封装设备 ops)+ `CharSink` 抽象(console 写槽,kernel 注入 Serial / host 注入 mock)→ 挂 `/dev` → 基础节点 `/dev/null`(1:3) `/dev/zero`(1:5) `/dev/console`(5:1),read/write 走设备;`stat()` override 填 `st_rdev`。新代码类化(非全局 static + 自由函数)。
>
> **本批不做（并行硬约束 / 留 F6 后续)**:
> - **不改 `fs/inode.hpp` 的 `InodeOps` 虚函数接口**(F10-M2 动态链接在用)——只加子类,签名一行不动;`st_rdev` 收进 ops 子类 `stat()` override,不加 `Inode` 字段。
> - **不做 mknod**（T3 的用户态创建设备节点,root only)——留 F6 后续（需 DevFS 写路径 + 权限)。
> - **不做块设备节点**（T2 的 `/dev/sda` `/dev/nvme0n1` 经 `IBlockDevice`)——留 F6 后续。
> - **不做 F6-M1 VFS 增强（dentry cache/symlink/link/flock)、M2 ProcFS、M4 tmpfs、ext4** ——各自单独收。
> - **`/dev/console` 本批 write→serial 输出**（基础节点)；接 `ConsoleTty` 真 stdin / `/dev/tty` / PTY 是 **F10-M3 TTY Phase2**,显式推迟不欠债。
>
> 详见 PLAN「🔄 F6-M3 DevFS」段。

## 目标

实现 DevFS，提供 /dev 下的设备文件。

## 任务清单

### T1: DevFS 框架

**文件**: `kernel/fs/devfs.hpp`, `kernel/fs/devfs.cpp`

```cpp
class DevFS : public FileSystem {
public:
    bool mount() override;
    Inode* lookup(const char* path) override;

    // 设备注册
    void register_char(uint32_t major, uint32_t minor, const char* name, InodeOps* ops);
    void register_block(uint32_t major, uint32_t minor, const char* name, IBlockDevice* dev);

private:
    struct DevEntry {
        char name[32];
        uint32_t major, minor;
        bool is_block;
        Inode inode;
        InodeOps* ops;        // 字符设备操作
        IBlockDevice* block_dev; // 块设备
    };
    DevEntry entries_[DEV_MAX];
    int entry_count_;
};
```

- [ ] DevFS 继承 FileSystem
- [ ] register_char / register_block 设备注册
- [ ] lookup 按 name 查找设备
- [ ] 设备的 read/write 委托给 InodeOps/IBlockDevice

### T2: 标准设备节点

| 设备 | Major:Minor | 类型 | 说明 |
|------|------------|------|------|
| /dev/null | 1:3 | char | 丢弃写入，读取返回 EOF |
| /dev/zero | 1:5 | char | 写入丢弃，读取返回零 |
| /dev/console | 5:1 | char | 系统控制台 |
| /dev/tty | 5:0 | char | 当前进程控制终端 |
| /dev/sda | 8:0 | block | 第一个 SATA 磁盘 |
| /dev/nvme0n1 | - | block | NVMe 磁盘 |

- [ ] null/zero 设备实现
- [ ] console 设备（连接到 Framebuffer Console）
- [ ] 块设备节点（通过 IBlockDevice 访问）

### T3: 设备文件 syscall 支持

- [ ] open("/dev/xxx") 路由到 DevFS
- [ ] mknod() syscall — 创建设备节点（root only）
- [ ] stat() 显示设备文件的 st_rdev（设备号）

### T4: 单元测试

- [ ] /dev/null 写入丢弃
- [ ] /dev/zero 读取返回零
- [ ] 块设备通过 /dev/sda 读写

## 产出物

- [ ] `kernel/fs/devfs.hpp` / `.cpp`
- [ ] 标准设备节点
- [ ] mount /dev 集成到启动流程
