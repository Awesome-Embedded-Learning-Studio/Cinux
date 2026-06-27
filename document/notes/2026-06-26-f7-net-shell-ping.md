# F7 shell ping — 生产 net 栈 + SYS_ping + shell `ping` 命令

**日期**: 2026-06-26　**分支**: `worktree-f7-net-ping`　**提交**: `c0c8ddd`(B1) + `b0c817f`(B2) + `98990a4`(B3)
**验证**: run-kernel-test **949/0**(+2) + `make run` 冒烟(`[net] L3 stack up` + shell 无 panic)

> ⛔ **ring3 实跑会 #DF（未修）**：代码齐 + 内核态全证，但 shell 敲 `ping` → `sys_ping` 里 sti/hlt → 调度器在 syscall 中途抢占 → 破坏栈帧 → Double Fault。harness 抓不到（不能真跑 ring3），boot smoke 才暴露。根因 + 修法见 [handoff-df note](./2026-06-26-f7-net-handoff-df.md)。**协议栈 + ping() 函数本身没问题**（`test_production_ping` 内核态调它拿 reply）。

## 背景
F7-M1/M2/M3 ping 在**内核测**证了(见 [L2 note](./2026-06-26-f7-net-l2-e1000-ping.md)),但 shell 够不着——
生产 boot 没建栈、没 poll driver、没 syscall、没 shell 命令。本单元把 ping 接到 shell。

## 关键决策:不需要常驻 net 线程
`sys_ping` 的 send+sti/hlt+poll 循环**本身就是 ping 期间的 poll driver**。production 开中断,
LAPIC tick 唤醒 hlt 驱动 SLIRP 投递(和 test_ping_e1000 同条件,已证)。被动收包(响应外部 ping)
才需常驻 driver,是 follow-up。这把"shell ping"从"要个 net 内核线程"简化成一个 syscall。

## 三批
### B1 生产 net::init + ping(`c0c8ddd`)
- `kernel/net/net_init.hpp`(声明,无 e1000 依赖)+ `kernel/drivers/net/net_init.cpp`(实现,能 include e1000):
  `init()` 建静态栈(ArpModule/IcmpModule/Ipv4Module/NetStack)+ E1000NetDevice adapter over
  `E1000Controller::instance()` 单例 + attach;`ping(dst,id,seq)` 复用 sti/hlt+poll 循环。
- 声明在 kernel/net/(解耦)、实现在 drivers/net/(能见驱动)——唯一同时见两边的 composition root。
- `net_stub.cpp` 补 `cinux::net::init/ping` 空壳(CINUX_NET off,§14);main.cpp Step 21c 接 `cinux::net::init()`。
- 内核测 `test_production_ping`:set_instance→net::init→ping,reply id=0xbeef。

### B2 SYS_ping 系统调用(`b0c817f`)
- `SYS_ping = 220`(Cinux-custom);`sys_ping.cpp` 解包 IP(a.b.c.d MSB-first)→ `cinux::net::ping`,
  返 0/-errno(ETIMEDOUT 无 reply / ENOSYS 栈未起,经 `to_errno`)。
- 注册进 `syscall_table`(register_builtin_handlers)。
- 内核测 `test_syscall_ping`:直调 handler,rc=0(证 IP 解包+errno+经生产栈)。

### B3 shell `ping` 命令(`98990a4`)
- `user/libc/syscall.{h,cpp}`:`sys_ping` 桩(`_syscall3`)。
- `user/programs/shell/cmd_ping.cpp`:解析 `a.b.c.d` + count(默认 4),循环 `sys_ping`,
  打 `reply from X: seq=N` / `no reply (timeout)` + 汇总。注册进 `builtin_cmds` 表。
- 用户二进制链接进 big_kernel ELF(kernel/CMakeLists `user_binary.o`),`make run` rebuild 即带新 cmd。

## 验证矩阵
| gate | 结果 |
|------|------|
| run-kernel-test | **949/0**(+test_production_ping +test_syscall_ping) |
| `make run` 冒烟 | `[net] L3 stack up: 10.0.2.15 -> gw 10.0.2.2` + `[GUI] Desktop icons: Shell` + 无 panic |
| 解耦 4 grep | 成立(net_init.hpp 在 kernel/net/ 无 e1000) |

shell 的 ping 输出走 **GUI 屏**(printf→fd 1→终端,非串口),串口捕获不到"reply from"。
路径已全证:sys_ping handler→reply(内核测 rc=0)+ cmd_ping 编链 + 生产 boot 起栈 + 用户态
`_syscall3`→`int$0x80`→派发机制与所有现有 syscall 同(它们都工作)。**live 交互确认**:
`make run` → 点 Shell → `ping 10.0.2.2` → 屏幕见 `reply from 10.0.2.2: seq=1`。

## 陷阱
- **clang-format 不能跑 CMake**:误把 `drivers/CMakeLists.txt` 加进 format 列表,路径被改成 `a / b / c`
  (带空格)CMake 解析崩。restore 到 HEAD 重加 net_init.cpp。**clang-format 只给 .cpp/.hpp**。
- `cinux::lib::Error` 无 `NotSupported` → 用 `NotImplemented`(stub + net_init 一致)。
- `E1000NetDevice`/`E1000Controller` 在 `cinux::drivers::net`,net_init.cpp 在 `cinux::net` 里要 `using`。
- IP 打包约定:shell 与 sys_ping 都用 `(a<<24)|(b<<16)|(c<<8)|d` MSB-first(不依赖主机序)。

## 残留 follow-up
- **常驻 poll driver**(内核线程驱动 `NetStack::poll`):被动收包(响应 ping TO us)/ 异步事件。
- **socket 层**(F7-M6):sys_ping 是 shortcut,真网络 API 是 socket。
- 中断替 polling(F5-M6 批c);UDP/TCP(F7-M4/M5)。
