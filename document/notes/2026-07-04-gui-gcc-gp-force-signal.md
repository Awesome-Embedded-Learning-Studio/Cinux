# GUI shell gcc #GP 兜底 + 根因链(/tmp + signal force)

> 2026-07-04,接 [GUI shell ls denied](2026-07-04-f-usability-gui-shell-ls-denied.md)。GUI shell
> 跑 `gcc` 编译触发 #GP 死循环,把 shell + OS 一起拖死。排查 + 修复 signal 兜底(本次重点)
> + rootfs 闭包。

## 现象

GUI shell(`run-single` + `rootfs-gcc.ext2`)敲 `gcc -fno-pie -no-pie /hello.c`:
- gcc 起来,探测 glibc-hwcaps / lto-wrapper
- 然后 #GP 死循环(同 rip,日志刷几百行)
- shell + OS 卡死(VNC 无响应)

## 排查(实证,加仪器不猜测)

### 1. #GP 指令字节(GP-DIAG 仪器,#GP handler 限次前 2 次防炸)

```
[GP-DIAG] rsp%16=0 mxcsr=0x00001f80 insn(16): f4 bf 7f 00 00 00 e8 43 00 0c 00 e8 65 ff ff ff
```
- `rsp%16=0` 栈对齐 OK
- `mxcsr=0x1f80` SSE 默认正常
- 第一条 **`f4` = HLT**(ring 3 特权指令 → #GP)

### 2. hlt 位置(objdump)

`objdump -d /usr/bin/gcc` → hlt 在 `0x40b245`,前一行 `call *0x19bd43(%rip)`(GOT 0x5a6f88 = `__libc_start_main@GLIBC_2.34`)。经典 `_start` 尾巴:`... call __libc_start_main; hlt`(noreturn 占位)。**gcc 的 `__libc_start_main` 返回了**(该 noreturn)→ 撞 hlt。

### 3. auxv 对比(排除 auxv)

do_execve_kernel 加 auxv 仪器,gate(GUI=OFF)拿基线 vs GUI:
```
gate gcc: entry=0x40B220 phdr=0x400040 phnum=16 phent=56 base=0x10000000 interp=1 stackx=0
GUI  gcc: entry=0x40B220 phdr=0x400040 phnum=16 phent=56 base=0x10000000 interp=1 stackx=0  ← 完全相同
```
auxv 公共 execve 设,一样。**auxv 排除**。

### 4. fpu 重置(排除)

execve 不重置 fpu_state(gcc child 继承 shell TaskBuilder fxsave 的 kernel-SSE 残留)。给 enter_loaded_program 加 `fninit + fxsave`。重跑——还 #GP。**fpu 排除**(但修复保留:execve 该给干净 FPU,对齐 Linux)。

### 5. SYSTRACE + WTRACE 定位 gcc 错误

syscall_dispatch 加 trace(pid>=3 fork child,前 80 次 + write buffer dump):
```
ioctl(16) → ENOTTY          (PTY slave TCGETS,glibc isatty 探测)
write(1)  → ret=65
[WTRACE] write fd=2 len=65: 'Cannot create temporary file in /tmp/: No such file or directory'
tgkill(234) → ENOSYS 循环   (gcc abort 想自杀,tgkill 没 stub)
#GP hlt
```

**gcc 写 /tmp 失败**(/tmp 没挂载)→ 进入 abort 路径。

## 根因(多层)

1. **/tmp 没挂载**:gcc 写临时文件失败 → abort
2. gcc abort 路径用 `rt_sigprocmask` 改 signal mask(屏蔽了 SIGILL)
3. **handle_gp 用 `signal_send`(force=false)**:sig_pending 投了 SIGILL,但 `signal_pick` 的 `avail = pending & ~sig_blocked` 把 blocked SIGILL 过滤掉 → 永不 deliver → 永不 default-kill → gcc 在同 rip livelock → 拖死 shell + OS

## 修复

### 兜底(本次重点):handle_gp force_send

`exception_handlers.cpp:244` `signal_send(kSigill)` → `signal_force_send(kSigill)`。

`force_send` 设 `sig_forced`;`signal_pick` 的 `avail = (pending & ~blocked) | forced` 绕过 block mask → SIGILL 一定 deliver → `signal_exec_default` → `exit_and_reap_current` → Zombie → shell waitpid reap。

**对齐 handle_pf**(`page_fault.cpp:218/396` 已用 force_send —— #PF 的 SIGSEGV 一直兜住,gate 的 sh segfault default-kill 就是这条)。

**铁律:同步异常(#GP/#PF)必须 force 投递**。进程能 block 自己的信号,但同步 fault 的 SIGILL/SIGSEGV 不能被 block 屏蔽,否则进程在 faulting rip livelock,带崩系统。Linux `force_sig_info` 同理。

### 附带(让 gate GUI=OFF gcc PASS,排除 gcc 闭包问题)

- **extract.sh `*_asneeded` 路径**(commit `5ded1d2`):`libgcc_s_asneeded` / `libatomic_asneeded` 在 gcc private libdir(`/usr/lib/gcc/<tuple>/<ver>/`),非 `/usr/lib`。原硬编码 `/usr/lib/` 静默失败(`|| true`),ld `cannot find -lgcc_s_asneeded`。改用 `copy_gcc_file`(走 `gcc -print-file-name`)。
- **rootfs-gcc.ext2 crt1.o**:旧 rootfs(7/3 build)的 `crt1.o` 是 buggy **executable**(带 PT_INTERP,该是 relocatable)。`extract.sh` 现产 relocatable,重 assemble 修。ld 之前报 `cannot use executable file 'crt1.o' as input` + 一堆 multiple definition。

## 验证

`gcc -fno-pie -no-pie /hello.c` → 串口 `[SIGNAL] default kill: tid=7 'shell' by SIG4` + **shell prompt 回来**(继续在线,能敲下一条命令)。gcc die(Zombie),shell waitpid reap,OS 不崩。

## 教训

1. **同步异常必须 force signal**:进程能 block 自己信号,但 #GP/#PF 的 SIGILL/SIGSEGV 不能被 block 屏蔽,否则 livelock 带崩系统。Linux `force_sig_info` 同理。**handle_gp 之前漏了 force(handle_pf 有)**,这次补齐。
2. **不猜测,加仪器**:#GP 指令字节(GP-DIAG)+ SYSTRACE + WTRACE 三连,直接钉死 `hlt` + `/tmp` + `tgkill`。前几轮推理(fpu/auxv/CoW)全错。
3. **串口 vs PTY**:GUI shell 的 write 到 PTY slave,VNC terminal 才看得到;串口看不到。卡死时(VNC 无响应)只能加 write trace dump buffer(本次 WTRACE)。

## follow-up

- **/tmp 内核侧已排除(下批 GUI 实测)**:排查时一度以为「gcc 写 /tmp 失败是因为 /tmp 没挂」,但实测 `init.cpp:75` 的 `tmpfs::init()` 在 boot 时就把 tmpfs 挂到 /tmp 了(GUI 和 gate 都走这条)。更硬的反向证据:gate 的 `cinux-usability-test.sh:43` 跑 `gcc -fno-pie -no-pie /hello.c -o /tmp/a.out && /tmp/a.out` **端到端 PASS**(记忆 b4a 两 leg 1108/0),而 gcc 的 cc1/as 用 mkstemp(`O_EXCL`)在 /tmp 创临时文件——**绝对路径 + O_EXCL + /tmp 在内核是工作的**。静态复核:`vfs_resolve("/tmp/ccXXX")` 正确命中 tmpfs(longest-prefix + 边界 `/`);`TmpFs::make_node` 只返 AlreadyExists/成功,从不返 ENOENT;`do_openat_kernel` 全程不识别 O_EXCL(忽略=当 O_CREAT,只会让该 EEXIST 时误成功,产不出 ENOENT)。原两条假设(vfs_resolve 绝对路径没走 tmpfs / tmpfs create O_EXCL 坏)**都排除**。GUI 失败是环境性的,嫌疑在 `shell_child_entry`(`kernel/gui/gui_init.cpp:84`)的极简 env(只有 `PATH=/bin:/sbin:/usr/bin:/usr/sbin`,无 HOME、无 cwd)+ GUI 模式 `launch_userspace` 直接 return 导致 PID1 走 `Scheduler::exit_current` 退出(没有 init 进程)。下批:重加 OTRACE + 临时改 `shell_child_entry` 自跑 `gcc /hello.c`,run-single 实测钉死现状。
- **串口回显 shell I/O**:本次 PTY slave write mirror 到 console 是临时 hack(帮卡死时串口看 shell 输出),已随诊断仪器删。要做正式 debug 基建留后续。
- **删诊断仪器**:已完成(commit 本批时清掉 GP-DIAG / auxv / SYSTRACE / OTRACE / WTRACE / pty mirror)。
- **execve 重置 fpu_state**:本次 fpu 重置没修 #GP(根因 signal force),但 execve 该重置(对齐 Linux),**保留并随本批提交**。
- **tgkill(234) 没 stub**:gcc abort 用,ENOSYS 现在,补 stub 改善(次要)。

接 [GUI shell ls denied](2026-07-04-f-usability-gui-shell-ls-denied.md)。方法论同 [[dont-ask-whether-to-investigate]]。
