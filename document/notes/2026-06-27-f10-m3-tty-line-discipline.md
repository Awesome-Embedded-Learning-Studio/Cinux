# 【回补】2026-06-27 F10-M3 TTY 行规范里程碑 — Phase 1 立项 + 批1/2/3

**日期**:2026-06-27　**分支**:`feat/f10-tty-dyn`(从干净 main `295d536` 拉)
**提交**:7c1fd5b(立项) / 53ef726(批1) / ced0066(批2) / 35cb419(批3)
**验证**:批1 `test_host` ctest PASS + `run-kernel-test-all` 955/0 两 leg;批2/批3 `run-kernel-test-all` 955/0 + `-smp2` 零回归

## 背景:为什么 CinuxOS 需要 TTY

F10-M1 已经让 CinuxOS 能跑 musl 静态用户程序(`Hello from musl on CinuxOS!`)。但
stdin/stdout 的现状是**硬编码 + 简陋**的,Plan 里 grep 坐实的家底:

- **stdin** 没有设备节点 / 没有 DevFS。`sys_read` 对 `fd==0` 走的是键盘 PS/2 环形
  buffer 的 **spin 轮询**:没键就空转 100 万次,然后返 0。**返 0 被 musl 当 EOF**
  ——所以交互式程序根本读不到真实输入。
- **stdout**(`fd==1`)直接走 `kprintf`,没有行缓冲这回事。
- **ioctl** 全返 `-ENOTTY`。musl 启动会探 `TIOCGWINSZ`(拿终端窗口大小),失败就退回
  全缓冲,`printf` 输出不及时。
- **没有行规范**:键盘敲一个字符就交一个字符,没有退格编辑、没有回车成行提交、没有
  Ctrl+C 信号——跟一个真终端差远了。

好消息是地基大多现成:F3 的信号 + 进程组 + killpg 已经做完;F3-M3 还预埋了一个
`Task::controlling_tty` 字段(-1 表示无);Console 已经有 echo sink。差的就差一个
**真 TTY 子系统**把键盘 → 行规范 → 进程 这条链接通。

用户拍板的方向:先做 TTY+ioctl,**PTY 和 `/dev/*` 节点留到 F6 DevFS**(没有设备 inode
这一层,PTY master/slave 建不起来)。用 console TTY 单例 + `controlling_tty` 字段
绕开 DevFS,功能完整不欠债。这就是 Phase 1 / Phase 2 的切分。

## 目标(立项 7c1fd5b 拆的 7 批)

立项时 PLAN 拆了批 0(立项)到批 6(收尾),规划了:行规范核心 → 接键盘回显 → stdin 阻塞读
→ ioctl 实命令(TCGETS/TCSETS/TIOCGWINSZ) → Ctrl+C 信号投前台组 → 收尾。

**实际只交付了批 1/2/3**(行规范 + 接键盘 + 阻塞读 + Ctrl+D EOF)。批 4/5/6(ioctl 实
命令、Ctrl+C → SIGINT 经 killpg、收尾)到 2026-06-29 仍未落。这篇笔记回补的是已交付
这三批,落了多少、怎么落、为什么这么落;剩下的留到末尾「残留」。

## 实现

### 批1(53ef726):TTY 行规范核心 + termios UAPI + host 单测

新建 `kernel/drivers/tty/tty.{hpp,cpp}`。这一批**只做纯逻辑**,不接任何内核依赖
(不碰 `sys_read`、不碰键盘、不碰 Console),目的是能直接在 host 链真码做单测。

**termios UAPI**:`tty.hpp` 定义了照搬 Linux `<asm-generic/termbits.h>` 的
`struct Termios`(iflag/oflag/cflag/lflag/c_line + `c_cc[19]`)+ `c_lflag` 位常量
(`kIcanon`/`kEcho`/`kEchoe`/`kEchok`/`kIsig`)+ `c_cc` 索引(`kVintr`/`kVerase`/
`kVeof`/`kVsusp`/`kVkill`/`kVwerase`)+ 对应的 ASCII 控制字节(`^C`/`^D`/`^\`/`^Z`/
DEL/`^U`/`^W`)。`make_default_termios` 给出默认的 cooked 模式
(`ICANON|ECHO|ECHOE|ECHOK|ISIG`)。

**关键设计:回显和信号都解耦**。这是这一批最重要的一步。`TTY` 类自己**不直接**
调 `kprintf`(那是内核的东西)、也**不直接**调 `signal_send`(那是 `proc` 的东西)。
- 回显走**注入的 callback**:`set_echo_sink(void (*emit)(char, void*), void* ctx)`。
  内核里注入 `kprintf` sink,host 单测里注入一个 capture buffer。
- 信号走**枚举**:`TtySignal`(kNone/kSigint/kSigquit/kSigtstp)。行规范检测到 `^C`
  返回 `InputResult::kSignal` 并记下 `pending_signal_`,**谁注入怎么处理是上层的事**。

这样 `tty.cpp` 是纯函数,host test harness 能直接链它跑真码单测,而不是手搓 mock。

**行规范状态机**(`input_char`):ICANON 下逐字节喂——
- 换行符 → 把 `line_buf_` 整行 commit 进 cooked 环形队列(换行符也算交付的一部分),
  回显,返回 `kLineReady`;
- VEOF(`^D`):空行 → 置 `eof_pending_` 返 `kEof`(EOF);非空 → commit 已缓冲内容
  (**不带尾随换行**,这是 `^D` 提交的语义);
- VERASE(DEL)→ `line_len_--` + ECHOE 三连显 `\b \b`(退格-空格-退格);
- VKILL(`^U`)→ 整行清掉;
- 可打印 → 追加 `line_buf_` + 回显;
- ISIG 位开时,`^C`/`^\`/`^Z` **根本不进 line_buf**,直接产 `kSignal`。
- 非 ICANON(raw)模式每字节直通 cooked 队列。

`read_cooked(buf, maxlen)` 从 cooked 环形队列拷出已提交的字节。

**host 单测** `test/unit/test_tty.cpp` 9 例:默认 termios 校验、行积累回显、退格编辑、
`^C` 产信号、`^D` 空行 EOF、`^D` 提交无换行、`^U` 清行、raw 直通、行缓冲溢出丢弃。
ctest PASS。

`tty.cpp` 这一批**加进了 `big_kernel_common`(编译进内核)但没接线**,所以是零行为变,
run-kernel-test-all 955/0 + `-smp2` 不回归。

### 批2(ced0066):接通 keyboard → TTY + Console 回显(stdin 走行规范)

这一批把批1 的纯 TTY 接到真实的键盘 IRQ 和 `sys_read`。新建
`kernel/drivers/tty/console_tty.{hpp,cpp}`:系统 **console TTY 单例** `g_console_tty`
+ 三个出口:`console_tty()` 拿实例、`console_tty_init()` 接线、`console_tty_read()`
批2 先不做阻塞(留批3)、`console_tty_input(char c)` 喂字节。

**接线两处**:
- **echo sink → kprintf**:`console_tty_init()` 给 TTY 注入 `echo_via_kprintf`,让回显
  的字符走和 stdout 同一个 sink(串口 + 注册的 Console)。**关键是 kprintf/Console::putc
  是 lock-free**——因为 keyboard 是 IRQ 上下文,在 IRQ 里调一个拿锁的打印会死锁;lock-free
  下最多偶发一个瞬时光标错位(不致命)。
- **VERASE = 0x08**:`console_tty_init` 里把默认 termios 的 VERASE 从 DEL(0x7F)改成
  `^H`(0x08)。**因为键盘驱动对 Backspace 键发的是 0x08 不是 DEL**,不改的话按退格
  不触发编辑(行规范认的是 DEL)。这是「设备层 vs UAPI 默认值」的一个小坑。

**键盘 dispatch**([../../kernel/drivers/keyboard/keyboard.cpp](../../kernel/drivers/keyboard/keyboard.cpp)
`dispatch_key`):只在 key **press 且 ascii != 0** 时,把 ascii 喂给 `console_tty().input_char()`
(行规范:回显/退格/清行/信号都在这里发生);`'\r'` 转成 `'\n'`。

**`sys_read` fd==0**:改读 `console_tty().read_cooked()`,**这一批还保留 spin 兜底**——
没行就 spin 100 万次后返 0。理由是批2 一次只接一个口子,先保证 headless 测试(没有键盘
输入、轮询 stdin)不挂死,阻塞留批3 叠加。`main.cpp` 在 Console init 之后、键盘中断
之前调 `console_tty_init()`(顺序要对:echo sink 依赖 kprintf/Console,要在键盘 IRQ 来
之前接好)。

run-kernel-test-all 955/0 + `-smp2` 零回归(测试无键盘输入,TTY 一直休眠)。

### 批3(35cb419):stdin 阻塞读替忙等 + 键盘 IRQ 唤醒 + EOF 信号

这一批干掉批2 留的 spin,**真正让 shell 读 stdin 时 CPU 不空转**,同时把 `^D` EOF
接通。这是这批的核心价值——修掉了「musl 把 spin 超时返 0 误当 EOF」的硬伤。

**TTY 侧加 EOF 标志**:`eof_pending_` + `take_eof()`。VEOF 在空行时置位
`eof_pending_`,`take_eof()` 返一次 true 然后清零——保证 **EOF 只交付一次**,且
read 返 0(EOF)和「暂时没行」(该阻塞)语义分得开。host 单测补了 take_eof 单次交付断言。

**console_tty 阻塞读**:加 `g_reader` 单读者指针(单读者 = shell 是 stdin 的唯一真实
读者;多读者 stdin 是 follow-up)+ 重写 `console_tty_read(buf, len)`:

```
for (;;) {
  InterruptGuard guard;               // 关中断
  n = read_cooked(buf, len);          // 有行?直接返
  if (n > 0) return n;
  if (take_eof()) return 0;           // ^D 空行 -> EOF
  g_reader = self;
  prepare_to_wait(self);              // 标记将要阻塞
  // ---- IRQs restored here ----     // 出 guard 前恢复中断
  schedule_blocked();                 // 切走,被 feeder 唤醒后循环再读
}
```

**这个 `InterruptGuard` + `prepare_to_wait` 的顺序是 F3 防丢失唤醒的铁律**:
check 有没有行、登记自己是读者、标记 Blocked 这三步必须在关中断下**原子**完成,
否则会出现「check 时没行 → 还没登记 → 键盘正好来了行 → feeder 找不到读者 → 我登记完
睡了」的丢失唤醒窗口。F3 的 `prepare_to_wait`/`schedule_blocked` 就是干这个的
(同 pipe write 唤醒 read 的模式,waitpid/pipe 已验过)。

**feeder 端 `console_tty_input(c)`**:喂完字节,如果结果是 `kLineReady` 或 `kEof`,
就 `unblock(g_reader)` 唤醒读者(清 `g_reader`)。键盘 `dispatch_key` 改调它(喂 + 唤醒
合一个)。

**`sys_read` fd==0**:改调 `console_tty_read()`(阻塞替 spin),删掉 `SPIN_WAIT_ITERS`
常量 + 不再 include `keyboard.hpp`。CPU 不再空转。

**SMP 注记**:`g_reader` 是单读者,**单 CPU 下关中断即竞态自由**;跨 CPU(SMP)下两个核
同时动 `g_reader` 没有锁保护——注释里明写这是 follow-up(需要 Mutex 风格的 spinlock
处理)。批3 在 `-smp2` 下不回归,因为测试根本不读 console stdin,阻塞路径全程休眠。

run-kernel-test-all 955/0 + `-smp2`(测试不读 console stdin,阻塞路径休眠)。

## 关键决策 / 教训

- **纯逻辑 + 注入式解耦是 host 单测的前提**。TTY 故意不碰 `kprintf`/`signal_send`,
  回显走 callback、信号走枚举。代价是上层(console_tty)要写一层胶水接线,但换来的是
  `tty.cpp` 能在 host 链真码单测 9 例,而不是 mock。这个模式以后接键盘/终端类逻辑都该照。
- **echo sink 必须是 lock-free 的(kprintf)**。keyboard 是 IRQ 上下文,IRQ 里拿锁打印会
  死锁。kprintf 本来就是逐字符 `Serial::putc` 无锁,正好能用。
- **设备层字节 ≠ UAPI 默认值**:键盘 Backspace 发 `^H`(0x08),termios 默认 VERASE 是
  DEL(0x7F)。两个约定不一致按退格就不工作。`console_tty_init` 里显式改 VERASE=0x08
  对齐键盘。这种「UAPI 默认值 vs 实际硬件」的错配要主动接缝,不能假设默认值就对。
- **EOF 是状态不是事件**:用 `eof_pending_` 标志 + `take_eof()` 一次性消费,把
  「read 返 0 = EOF」和「read 暂时没数据 = 该阻塞」两个语义分开。否则 `^D` 会让
  read 一直返 0 假 EOF。
- **阻塞读的丢失唤醒靠 F3 `prepare_to_wait` 原子性兜**(check + 登记 + 标 Blocked 关
  中断下原子)。这是 CinuxOS 已验证的标准缝,pipe/waitpid 都用它。
- **PTY / `/dev/*` 留 F6 DevFS 是正确切分**:PTY master/slave 硬依赖设备 inode 这一层,
  现在 CinuxOS 没有 DevFS,建了也是空中楼阁。Phase 1 用 console TTY 单例绕开,功能(行
  规范 + 阻塞读 + EOF)是完整的,不欠债。

## 残留(F10-M3 Phase 1 还没做的)

立项 PLAN 拆了批 0-6,**只落了批 0(立项)+ 批 1/2/3**。截至 2026-06-29 仍未交付:

- **批4 ioctl 实命令**:`TCGETS`/`TCSETS`/`TIOCGWINSZ`(+ `TIOCGPGRP`/`TIOCSPGRP` 用
  `controlling_tty`)。现在 `sys_ioctl` 仍全返 `-ENOTTY`,musl 探 `TIOCGWINSZ` 失败退
  全缓冲,printf 不及时。这是「行规范」之外 musl/glibc 行缓冲的真正卡点。
- **批5 信号投前台组**:行规范已经能产 `TtySignal`(`^C`/`^\`/`^Z`),但
  `TtySignal → signal_send → killpg(前台组)` 这条线**还没接**。批1 的枚举是为此预埋的,
  等批4 ioctl 的 `TIOCSPGRP`/`TIOCSCTTY` 把前台组/控制终端接通后再投信号。
- **批6 收尾 + 交织 F-VERIFY**:ROADMAP 标 Phase 1 ✅ + 本笔记 + sys_read/write/ioctl/
  keyboard 的 host 镜像副本链真码。

Phase 2(PTY / `/dev/ptmx` / `/dev/pts/N` / `/dev/tty` / `/dev/console` / TIOCSCTTY)
硬依赖 F6 DevFS,显式推迟。

## 关键文件

- [../../kernel/drivers/tty/tty.hpp](../../kernel/drivers/tty/tty.hpp) / [tty.cpp](../../kernel/drivers/tty/tty.cpp):termios UAPI + 行规范状态机(纯逻辑)
- [../../kernel/drivers/tty/console_tty.hpp](../../kernel/drivers/tty/console_tty.hpp) / [console_tty.cpp](../../kernel/drivers/tty/console_tty.cpp):console TTY 单例 + echo sink 接线 + 阻塞读 + IRQ 唤醒
- [../../kernel/drivers/keyboard/keyboard.cpp](../../kernel/drivers/keyboard/keyboard.cpp):`dispatch_key` press+ascii → `console_tty_input`
- [../../kernel/syscall/sys_read.cpp](../../kernel/syscall/sys_read.cpp):`fd==0` → `console_tty_read`(阻塞替 spin)
- [../../test/unit/test_tty.cpp](../../test/unit/test_tty.cpp):行规范 host 单测 9 例
- [../../document/todo/f10-userspace/02-tty.md](../../document/todo/f10-userspace/02-tty.md):Phase 1/2 范围切分
