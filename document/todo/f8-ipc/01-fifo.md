# M2: 命名 FIFO（mkfifo / 命名管道）

> F8-M2。命名 FIFO = 给 pipe 一个文件系统名字(对齐 Linux `S_IFIFO` + open 时按需建 pipe)。
> 复用 M1 增强后的 Pipe(T2 真调度阻塞 + T3 O_NONBLOCK)。open 语义(Linux fifo):**首端 open
> 决定建 Pipe**——首 reader/writer open 时内核建 Pipe 并在 FIFO 注册表登记;对端 open 复用同一 Pipe。
> 本里程碑范围:**机制 + kernel 内部 + 端到端 smoke**;两端都 open 前 open()阻塞 的完整 Linux 语义、
> 用户态 shell 真闭环留后续。

## 任务清单

### T4: FIFO 核心(FifoRegistry + FifoOps)

**文件**: `kernel/ipc/fifo.{hpp,cpp}`(新)

```cpp
class FifoRegistry {  // 路径 -> Fifo*(持有 Pipe + reader/writer 计数)
    ErrorOr<Fifo*> get_or_create(const char* path);
    void remove(const char* path);
};
class FifoOps : public InodeOps {  // open(inode) cloning 建/复用 Pipe
    ErrorOr<Inode*> open(Inode* inode) override;
};
```

- [ ] `FifoRegistry`:路径 → `Fifo*`(持有 Pipe + 端计数),get_or_create/lookup/remove。
- [ ] `FifoOps::open(inode)` cloning —— 首读者/写者建 Pipe(复用 Pipe + PipeReadOps/PipeWriteOps),返 per-open read/write inode(对齐 /dev/ptmx cloning 范式,PTY 刚加的 InodeOps::open 扩展点)。
- [ ] `fs_private` 存 `Fifo*`(避免改 InodeType enum)。
- [ ] host 单测:FifoRegistry get_or_create/remove + 首读者建 Pipe round-trip(纯逻辑,host-testable)。

### T5: mknod / mkfifo 命名节点

**文件**: `kernel/syscall/sys_mknod.{hpp,cpp}`(新) + `kernel/arch/x86_64/syscall.cpp` + `syscall_nums.hpp`

- [ ] `SYS_mknod = 133`(Linux x86_64 号,新)注册 + `sys_mknod` handler。
- [ ] `S_IFIFO`(0x1000)mode → 在 FifoRegistry 命名建 FIFO;其它 mode 暂 NotImplemented(本里程碑只做 FIFO)。
- [ ] mkfifo(path, mode) = mknod(path, S_IFIFO|mode, 0) libc 等价。
- [ ] FIFO 命名经 VFS lookup 命中 FifoOps(FIFO 可任意路径,不必落 /dev)。

### T6: 跨 fd round-trip + mkfifo 真测

- [ ] kernel test:mkfifo 建 FIFO → open 写端 → open 读端 → write/read round-trip → close。
- [ ] 两 leg `run-kernel-test-all` + note + ROADMAP F8-M2 ✅。

## 产出物

- [ ] `kernel/ipc/fifo.{hpp,cpp}` — FifoRegistry + FifoOps(T4)
- [ ] `kernel/syscall/sys_mknod.{hpp,cpp}` + 注册(T5)
- [ ] `kernel/test/test_fifo.cpp` — mkfifo + round-trip(T6)
