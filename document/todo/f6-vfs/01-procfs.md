# M2: ProcFS

> /proc 虚拟文件系统。暴露内核状态给用户态。
> 进程信息、内存统计、内核参数。

## 本里程碑范围（2026-06-30 立项，分支 worktree-f6-m2-procfs）

**只做进程自省（第一刀）**，照抄 F6-M3 DevFs 范式（`FileSystem` 子类 + 匿名 namespace `InodeOps` 子类 + boot 接线单独 `procfs_init.cpp`）：
- [x] `/proc` 根 readdir 枚举 pid（新增 `signal_nth_task_pid` accessor，`g_registry_lock` 下走到第 n 个，纯增量；snapshot 版因栈帧超 1024B 改 nth）。
- [x] `/proc/<pid>/` 目录（lookup 经 `signal_find_task_by_pid` 校验存活，对齐 Linux 只露活进程）。
- [x] `/proc/<pid>/stat`（简化：`pid (name) state ppid tgid uid gid`）+ `/proc/<pid>/cmdline`（返 `name` 尽力，Cinux Task 不存 argv）。
- [x] 叶 inode 身份用 `PID_MAX=256` 定长 pid 索引池（`ino=pid`/`fs_private=this`），SMP 安全 + 无泄漏。
- [x] mount /proc 集成到启动流程（init.cpp 挂 procfs::init()）。

**范围栅栏（留 follow-up，本里程碑不做）**：T2 静态信息节点（version/meminfo/cpuinfo/uptime/loadavg/cmdline）、T3 的 maps/fd/status、完整 Linux /proc/<pid>/stat 52 字段（Cinux 无 accounting）。详见 PLAN「🔄 F6-M2」段。

## 目标

实现 ProcFS，提供系统自省能力。类似 Linux /proc。

## 任务清单

### T1: ProcFS 框架

**文件**: `kernel/fs/procfs.hpp`, `kernel/fs/procfs.cpp`

```cpp
class ProcFS : public FileSystem {
public:
    bool mount() override;
    Inode* lookup(const char* path) override;

private:
    // /proc 下的静态文件
    struct ProcEntry {
        const char* name;
        Inode inode;
        int (*read_func)(char* buf, size_t len);  // 内容生成函数
    };

    ProcEntry entries_[PROC_MAX_ENTRIES];
    int entry_count_;
};
```

- [x] ProcFS 继承 FileSystem
- [x] mount() 注册标准条目
- [x] lookup() 按 name 查找
- [x] read() 调用对应生成函数

### T2: 标准条目

| 路径 | 内容 |
|------|------|
| /proc/version | 内核版本字符串 |
| /proc/meminfo | 内存统计（total/free/cached） |
| /proc/cpuinfo | CPU 信息（vendor/model/freq） |
| /proc/uptime | 系统运行时间（秒） |
| /proc/loadavg | 负载平均值 |
| /proc/cmdline | 启动参数 |

- [ ] 每个条目一个生成函数
- [ ] 格式参考 Linux /proc 对应文件

### T3: 进程条目

| 路径 | 内容 |
|------|------|
| /proc/[pid]/ | 进程目录 |
| /proc/[pid]/status | 进程状态（PID/PPID/state/name） |
| /proc/[pid]/maps | 内存映射（VMA 列表） |
| /proc/[pid]/fd/ | 打开文件列表 |
| /proc/[pid]/stat | 简洁进程状态（一行） |
| /proc/[pid]/cmdline | 进程命令行 |

- [x] 数字目录名解析为 PID
- [x] 从 Task 结构体提取信息（stat/cmdline；status/maps/fd 见 follow-up）
- [ ] maps 列出所有 VMA 区域

### T4: 单元测试

- [x] mount /proc 成功
- [ ] 读取 /proc/version 有内容（静态节点 follow-up）
- [ ] /proc/1/status 显示 init 进程（status follow-up；stat 已有）
- [x] ls /proc 显示所有条目（readdir 枚举活 pid）

## 产出物

- [x] `kernel/fs/procfs.hpp` / `.cpp`（+ `procfs_content.{hpp,cpp}` / `procfs_init.cpp`）
- [x] 标准 + 进程条目（进程条目 stat/cmdline 已交付；静态标准节点 follow-up）
- [x] mount /proc 集成到启动流程
