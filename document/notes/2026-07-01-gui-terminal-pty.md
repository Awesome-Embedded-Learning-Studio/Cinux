# GUI 终端 PTY 替管道(busybox sh 真 tty)+ uname/poll/接 boot

> 2026-07-01。PR#59 合 main。把 GUI 终端的 pipe-backed shell 换成 PTY(真 tty),让 busybox sh isatty(slave)==true → 交互模式 + 行缓冲输出刷新。加 uname(63)/poll(7) stub + /bin/sh=busybox 接 boot 路径。PTY slave 空读阻塞(EOF→exit 修复)+ stdio ioctl 走 fd table(双 echo 修)。

## 为什么管道不行

GUI 终端原来给 shell stdin/stdout 各一根管道(Pipe)。问题:管道不是 tty → `isatty(0)==false` → busybox ash 非交互(无 prompt/无 echo) + musl stdout **全缓冲**(不 flush,输出卡在 stdio buffer)→ 用户看不到输出也敲不了。

- `-i` flag 没用(echo 是 TTY 驱动干的,不是 ash)。
- local echo 没用(`process_char` 只认可打印字符,`\n` 被忽略;且 musl 全缓冲输出照样不刷)。

## PTY 替管道(`14f886b`)

gui_init.cpp `create_shell_terminal`:pipe → `pty_alloc()` + `pty_master_inode(idx)`(终端持,双向)+ `pty_slave_inode(idx)`(shell fd 0/1/2,isatty 真)。terminal.cpp:`stdin_pipe_`/`stdout_pipe_`(Pipe*)→ `master_inode_`(Inode*),`on_key` 写 master(master_write→slave TTY cook+echo),`poll_output` 读 master(出 echo+程序输出)。

**效果**:slave 是真 tty → ash 交互 + musl 行缓冲 → echo(prompt/命令)按行刷出。

## PTY slave 空读阻塞(`2760d35`,Codex 修)

第一版 PTY 终端 busybox sh 一启动就 `sys_exit(0)`——PTY slave 的 `slave_read` **无 cooked line 时返回 0**,ash 把 0 当 EOF → 干净退出。修法:`PtySlaveOps::read` 加阻塞(无数据时 `prepare_to_wait`/`schedule_blocked`,等 master_write 提交 cooked line 后唤醒)。与 pipe/TcpSocket recv 同范式。

**诊断 prompt**(自包含 `.claude/codex-debug-prompt.md`)交给 Codex 执行,Codex 定位 + 修 + 验证两 leg。

## PTY stdio ioctl 走 fd table(`5a2457b`,用户修)

double echo(fprintf 走 PTY master→slave TTY echo 一次 + write 走 stdout pipe→terminal write 一次)。修法:stdio ioctl(TCGETS/TCSETS)优先查 fd table(fd>2 走 inode→ops->ioctl),不硬编码 fd 0/1/2。详见 [2026-07-01-gui-pty-stdio-ioctl-echo](2026-07-01-gui-pty-stdio-ioctl-echo.md)。

## uname(63)+poll(7) stub(`6ec214d`)

busybox sh 启动调 uname(取 prompt hostname)+ poll(stdin 就绪再 read)。两个 unhandled syscall:
- uname:填 utsname{sysname=CinuxOS, nodename=cinux, release=0.1.0, machine=x86_64}。
- poll:**stub**(每 pollfd 报 POLLIN 就绪,无 wait;sh 随即 read 阻塞等真输入)。真 poll 留 F8-M5 epoll/poll。

## /bin/sh=busybox 接 boot(`3a470b1`)

create_ext2_disk.sh:busybox 在时 /bin/sh=busybox(argv[0] basename "sh" → ash applet);否则 user_shell(CI 安全)。shell_launch(非 GUI)+ gui_init(GUI terminal)都 execve /bin/sh → 两路都得 busybox sh。

## ext2 镜像 busybox 硬链接(`a440f8e`)

busybox 单二进制 → 各 applet 通过 `/bin/busybox <applet>` 或硬链接(`/bin/ls`→busybox)调。create_ext2_disk.sh 补 busybox applet 硬链接到 ext2,让 `ls`/`cat` 等可直接调(不靠 `busybox ls`)。

## follow-up

- PTY 真 poll/wait-queue(F8-M5 epoll/poll)→ 替 poll stub。
- PTY 做 controlling terminal(TIOCSCTTY → job control)。
- busybox sh 行编辑(arrow/history)需 termios ICANON 真实 → PTY slave TTY 已支持(Phase 2)。
