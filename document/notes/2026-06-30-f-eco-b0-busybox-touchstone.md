# 2026-06-30 F-ECO 批0:busybox 试金石起步 — echo 跑通 + 2 内核健壮性修复

> F-ECO(用户生态试金石)主线第一关 busybox 的第一批:摸底。结论:**busybox
> echo 在 CinuxOS 上真跑通了**(fork+execve+musl 运行时+write 到串口),试金石
> 立刻挖出 2 个内核健壮性 bug + 1 个 musl/编译器兼容问题 + 2 个 syscall 缺口。

## 编译路线

- **clang --target=x86_64-linux-musl --sysroot=build/musl-sysroot -static**。
  musl-gcc 在 GCC 16 坏(`-latomic_asneeded` 找不到),clang + musl sysroot 是
  唯一能用的 C 静态编译路线(已用最小 hello 验证:静态 ELF + host 跑 exit=42)。
- busybox 1.39.x(git master),`allnoconfig` + `CONFIG_STATIC/ECHO/CAT/LS/TRUE/FALSE`,
  产物 145 KB,host 验证 echo/cat/ls/true(0)/false(1) 全对。

## smoke 框架(照搬 musl hello smoke 范式)

`main_test.cpp` 的 `musl_hello_smoke_entry` 是参照(mount ext2 → fork → child
装 AS + launch_user_program → parent 轮询 waitpid)。加 `CINUX_BUSYBOX_SMOKE`
gate(默认 OFF),echo 5 iter gate + ls 1 次观察。plumbing:create_ext2_disk.sh
加 busybox 参数 + qemu.cmake artifact + 两层 CMakeLists gate。

## 摸底挖出的真问题(试金石价值:写 100 个自写 unit test 都测不出)

### 问题 1:用户态 #GP → panic(handle_gp)

echo iter1 撞 RIP=0x406671,反汇编 = `hlt`(特权指令,ring3 → #GP error=0)。
`hlt` 是编译器把 UB/unreachable 编成的 trap。原 `handle_gp` 用户态无条件
`panic()` —— 一个用户程序执行非法指令不该死机。

**修**:`handle_gp` 加用户态分支(`cs&3==3` + 有 task)→ `signal_send(kSigill)`
+ return,参照同文件 PF→SIGSEGV 范式。ISR stub 的 `signal_check_deliver_isr`
投递(default kill)。kernel 不再 panic。

### 问题 2:信号死 child 不可 waitpid 收割(exit_current 跳过 Zombie)

修了问题 1 后,echo child SIGILL 死,但 smoke 超时 —— parent waitpid 收割不到。
根因:signal default kill(`signal.cpp:282`)走 `Scheduler::exit_current()`,它
设 `TaskState::Dead` + deferred-free,**跳过 Zombie 契约**,而 waitpid 找的是
`state==Zombie`(process_new.cpp:185)。所以信号杀死的 child 永远 NotExited,
parent spins 到超时。hello 走 `sys_exit`(设 Zombie + notify),所以没这问题。

**修**:default kill 改走 `sys_exit(sig)` 全套(exit_status=sig + SIGCHLD + Zombie
+ dequeue + unblock parent + yield),对齐 Linux WIFSIGNALED。验证:`sys_exit(4)`
→ Zombie → `[WAITPID] reaped child pid=10 exit_status=4`,smoke 跑完不超时。

### 问题 3:musl mallocng hlt trap(GCC 16 -O2)

addr2line 定位 RIP=0x406671 → **musl mallocng `alloc_slot`(malloc.c)**。关键反转:
musl libc.a 是 **GCC 16.1.1 编的**(不是 clang!),但新 GCC 也把 mallocng 的
UB/`__builtin_unreachable` 编成 `hlt`(alloc_slot 含 3+ 个 hlt)。hello 不 malloc
没崩,busybox 一 malloc 就走到 hlt trap。

**修**:musl 重编 `-O1`(build-musl.sh `CFLAGS="-O1"`)。malloc 状态机绕开 hlt
那条 UB 路径,echo 的 malloc 跑通。(alloc_slot 残余 6 个 hlt,-O1 未全消,但 echo
路径不触发;重 malloc 模式可能仍触发,follow-up。)

## 结果

```
[F-ECO] busybox echo smoke: 5 iterations
f-eco-busybox-ok      ← busybox echo 真写到串口(×5)
[F-ECO] busybox echo 5/5 PASS -> PASS
[TEST] ALL TESTS PASSED (exit code 0)
```

**busybox echo 在 CinuxOS 上真跑通**。两个内核修复 `run-kernel-test-all` 两 leg
全绿(无回归)。

## 试金石挖出的 syscall 缺口(ls,后续批)

```
[SYSCALL] unhandled syscall 72      ← fcntl(批4)
[SYSCALL] unhandled syscall 217     ← getdents64(批1 头号)
[SYSCALL] sys_exit(0)               ← ls 列空但 exit 0 = 假绿!
```

- **getdents64(217)** — 批 1 头号缺口。musl `opendir`/`readdir` 走 217(不是老
  getdents 78),CinuxOS 没实现 → ls 列空。
- **fcntl(72)** — 批 4 缺口。
- **ls 假绿**:getdents64 返 ENOSYS → ls 列空但 `exit 0`。**印证 README 准确性
  原则:退出码 0 不算过,输出+副作用精确匹配才算过**。ls 必须 gate 输出内容。

## commit

- `2ee610b fix(kernel)`:用户态 #GP→SIGILL + 信号死 child 走 Zombie 可 waitpid 收割
- `6513dbb feat(f-eco)`:busybox 试金石框架 + 立项文档
- 本批:`build-musl.sh` -O1(GCC16 mallocng hlt)+ 本 note

## follow-up

- 批 1:`getdents64`(217)→ ls 真跑通(强校验输出,防假绿)
- `signal.cpp:461` sigreturn bad-frame kill 同类 `exit_current` bug(走 Zombie)
- musl mallocng alloc_slot 残余 hlt(-O1 未全消,重 malloc 模式 follow-up)
- 批 4:`fcntl`(72)
- echo 用例强校验:当前 gate exit==0 + 串口可见输出;CI 化后要读串口精确比对
