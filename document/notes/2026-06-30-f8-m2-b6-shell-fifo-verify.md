# F8-M2 批6 — shell FIFO 验证(mkfifo + fifotest 命令)

> 2026-06-30，worktree `worktree-f8-pipe-fifo`。F8 shell 端验证补批(用户要在 shell 上验)。

## 背景 / 决策

批5 收官后用户问「能在 shell 上验吗」。查证:**user/libc 的 `open` 走 `SYS_open(2)` → `do_open_kernel`**
(已有 cloning),所以 shell 内置命令 open FIFO **已经过 cloning**——**①(do_openat 加 cloning)对 shell
验证不需要**(那是 musl/openat 路径,留 F10-M4)。这一轮只加 shell 命令,零内核改动。

> ① 仍是诚实 follow-up:让 musl 程序 open FIFO/PTY 经 openat 也触发 cloning。动 open 热路径,值单独一轮。

## 做了什么(纯用户态)

- **user/libc 加 `sys_mknod` wrapper**(SYS_mknod=133)+ `S_IFIFO`/`O_RDONLY`/`O_WRONLY`/`O_RDWR` #define。
- **`cmd_mkfifo`**:mknod(path, S_IFIFO|0666, 0) 建命名 FIFO(重复→EEXIST 报错,对齐 Linux)。
- **`cmd_fifotest`**:单进程端到端 smoke——mknod→open(O_WRONLY)→open(O_RDONLY)→write→read→print
  `hello from fifo!` + `fifotest: OK`。两端**同时开**(write 前 reader 已在),单次 blocking read 立即返
  缓冲字节,不阻塞。
- 注册到 main.cpp 命令表 + shell.hpp 声明 + cmd_help 文本 + user/CMakeLists.txt。

## 陷阱

- **cmd_fifotest 可重入**:最初想 `sys_unlink` 清旧 FIFO——但 **DevFS 没 unlink**(DevDirOps 只有
  readdir/stat),sys_unlink 对 FIFO 名 NotFound/ENOSYS,清不掉 registry。改为**忽略 mknod 的 EEXIST**:
  FIFO 名复用,共享 pipe 每次 read 排空,旧状态无害(重跑照样 round-trip)。
- **close fd 不清 pipe 的 open 标志**(匿名 pipe 同款 hobby-OS 限制)→ 故意两端同时开 + 单次 read,
  不依赖 close 行为,避开「read 阻塞等永不来的 writer」死锁。

## 验证

- `run-kernel-test-all` 两 leg **993/0** 零回归(本批零内核改动;shell ELF 重建 + ext2 镜像重打)。
- user_shell 编译绿,`fifotest`/`mkfifo` 符号链进 shell ELF。
- **机制级**(do_mknod + cloning + round-trip)由 kernel test_fifo(批5)+ host test_fifo 守。
- **shell 端 GUI 交互验证归用户**(shell 输出走 cgui 屏,串口捕不到;本环境无显示):
  `make run` → shell → `fifotest`(应打 `hello from fifo!` + `fifotest: OK`)、
  `mkfifo /dev/x`、`stat /dev/x`(st_mode S_IFIFO)。

## 收官

F8 shell 验证补批完成。累计 7 commit。**push/PR 归用户**。
