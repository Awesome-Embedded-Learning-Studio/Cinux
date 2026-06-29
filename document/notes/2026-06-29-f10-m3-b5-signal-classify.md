# F10-M3 批5 — 信号生成 + console_tty 类化(ConsoleTty)

> 2026-06-29。接批4(ioctl TCGETS/TCSETS/TIOCGWINSZ)。批5 把 Ctrl+C/quit/suspend 接成真信号投递(之前 console_tty 只唤醒 reader,不投信号),并按用户反馈把 console_tty 从 C 风格收进 ConsoleTty 类。

## 信号生成(批5 核心)

之前缺口:`console_tty_input` 只处理 `kLineReady/kEof` 唤醒 reader,**未处理 `kSignal`** → Ctrl+C 按了没反应(`TtySignal` 挂着没人投)。

修:
- `tty.hpp` 加 `TtySignal take_signal()`(消费并清 `pending_signal_`,对齐 `take_eof`)。
- `ConsoleTty::input` 处理 `InputResult::kSignal` → `take_signal()` → 映射 `Signal`(interrupt→SIGINT / quit→SIGQUIT / suspend→SIGTSTP)→ `killpg(foreground_pgid, sig)`。前台组未设(`==0`)回退 `reader_->pgid`。
- `killpg` 用 `irq_guard`(IRQ-safe 锁),keyboard IRQ 上下文调安全(嵌套 cli 无害)。
- `sys_ioctl` 加 `TIOCGPGRP/TIOCSPGRP`(读写 `foreground_pgid`,`copy_to/from_user` extable SMAP 安全)。

## 类化重构(用户反馈:都 C++ 了为什么不放类里)

之前 `console_tty.cpp` 是 C 风格:全局 `static g_console_tty`/`g_reader`/`g_foreground_pgid` + 自由函数 `console_tty_init/read/input`。批1-3 既有的,批5 加 `g_foreground_pgid` 放大了问题。

收进 `ConsoleTty` 类:
- 私有 `tty_`(TTY)/`reader_`(Task*)/`foreground_pgid_`(int)。
- 公开 `init()/read()/input()/tty()/foreground_pgid()/set_foreground_pgid()`。
- 全局单例 `ConsoleTty g_console_tty` + `ConsoleTty& console_tty()` 返引用。
- caller 改方法调用:`keyboard.cpp` `console_tty().input(c)` / `sys_read.cpp` `console_tty().read(...)` / `main.cpp` `console_tty().init()`。

**与 Mouse/Keyboard 全 static 的区别**(memory `static-not-singleton-for-singleton-hardware`):那俩是无状态/单值工具(系统唯一硬件,全 static 合理);console_tty 有 mutable 共享状态(reader/前台组跨方法共享),该类化。判断信号:全局 static 有 >1 个可变字段 + 跨自由函数共享 → 类化。批6 收尾扫同类异味。

## 验证

- `run-kernel-test-all` 两 leg:**967 passed / 0 failed** + AP readback PASS。
- `ctest` host:**62/62**。
- +3 测:`test_console_tty_ctrl_c_sends_sigint_to_foreground`(Task pgid=5 + set fg + input(^C) → 查 `sig_pending` SIGINT)+ Ctrl+Z→SIGTSTP + TIOCSPGRP 内核址 -EFAULT。机制测证信号真投(不只"绿")。
- 关 smoke 验证(本地无 /hello)。

## Follow-up

- **shell 设前台组**:完整 job control 需 shell fork 程序时 `TIOCSPGRP` 设前台组(否则 Ctrl+C 回退 reader=shell 组)。musl/glibc shell 会调;CinuxOS shell 是否调待验。
- **SIGTSTP 默认 Stop**:suspend 投 SIGTSTP,default disposition=Stop(F3-M4 Stopped 状态机)。shell job control 恢复需 SIGCONT。
- **winsize 真实几何 + TIOCSCTTY**:留 DevFS(F6)。

## 牵连

复用 F3(signal_send/killpg irq_guard + 进程组 pgid)+ F-EXTABLE(copy_to/from_user)+ 批1-3(console_tty 单例)。零回归(967/0 + 62/0)。
