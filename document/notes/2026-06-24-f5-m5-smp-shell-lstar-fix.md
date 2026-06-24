# F5-M5 -smp Shell 完全可用:AP 漏设 LSTAR(#DF)+ AP 缺 LAPIC timer(键盘不通) — 2026-06-24

> 分支 `feat/f5-m5-xhci-3`。本批收掉 [2026-06-24-f5-m5-usb-mouse-interrupt-fix.md](2026-06-24-f5-m5-usb-mouse-interrupt-fix.md) 之后 -smp 下 Shell 的**两个**遗留问题,让 `-smp 2` 点 Shell 既不崩、键盘也能输入。单核一直正常,两个都是「AP 没对齐 BSP 的配置」类问题。
>
> **TL;DR**:
> 1. **#DF**:AP 的 `usermode_init_asm()` 设了 STAR/SFMASK/EFER.SCE 但**漏了 LSTAR** → AP `LSTAR==0` → 用户任务在 AP 首个 `syscall` 时 `RIP←0`、`CS←0x10` 跳地址 0 的内核态 → #PF→#DF。修:ap_main 写 `LSTAR=syscall_entry`。
> 2. **键盘无响应**:AP **没有周期定时器**(BSP 靠 PIT,而 PIT 的 IOAPIC 重定向只到 BSP;LAPIC timer 从没编程)→ AP 无抢占 → shell 在空 stdin 上 `Pipe::read` spin-`hlt` 等 AP 定时器唤醒,等不到 → AP 死锁 → gui_worker(pin 在 AP)饿死 → 键盘事件不 pump。修:AP 在 ap_main 编程 LAPIC timer(周期),ISR 调 `Scheduler::tick`(镜像 PIT 的 early-EOI)。

## Part 1 — #DF:AP 漏设 LSTAR

### 现场

`-smp 2` shell 进入用户态、跑几条指令后首个 `syscall` 即崩:
```
[PROC] jumping to user mode: entry=0x400270
========== KERNEL PANIC ==========  Double Fault (error code=0x0)
RIP=0xFFFFFFFF810024FD (isr_pf_stub 内)  CS=0x10  RSP=0x7FFFFB000(用户栈!)
RCX=0xC0000101(=MSR_GS_BASE,percpu() 的 rdmsr 残值)
```
`RIP` 落 `isr_pf_stub`(无 IST)→ 内核态 #PF 用当前(用户)栈 push 帧,push 到未映射页再崩 → #DF(IST1,能 dump)。单核从不中。

### 诊断(debugcon 捕 live GS + 入口探针)

#DF handler 跑在 IST1(好栈),从 CPL=0 进不 swapgs → 此时 `rdmsr(GS_BASE)` = 内核真实 GS。经 `isa-debugcon`(0xE9→build/debug.log)逐层捕 `(GS_BASE, KERNEL_GS_BASE)` + 入口 tag:
- `launch@gs=(percpu[1],0)` —— AP1 内核态**正确**。
- `syscall_entry` 入口 swapgs 探针 `sc_swapped=none` —— **shell 的 syscall 从没进 syscall_entry**。
- `jtu`(jump_to_usermode sysretq 前)`rcx=0x400270 gs=(0,percpu[1])` —— 进用户态前 GS/目标**正确**。

sysretq 正确进了用户态,shell 跑起来后做 `syscall` 却不进 syscall_entry → **LSTAR 没指向它**。核实:`usermode_init_asm` 只写 STAR/SFMASK/EFER.SCE,**不写 LSTAR**;全仓库只 BSP 的 `syscall_init()` 写过。

### 根因 + 修法

AP `LSTAR==0` → 用户任务在 AP 上 `syscall`:`RIP←0`、`CS←STAR[47:32]=0x10`(内核态)→ 跳地址 0 → `percpu()`=0/`current()` 解引用 0 → #PF @ rip=0 → 用户栈连锁 → #DF。[ap_main.cpp](../../kernel/arch/x86_64/ap_main.cpp) 在 `usermode_init_asm()` 后补:
```cpp
write_msr(0xC0000082, reinterpret_cast<uint64_t>(syscall_entry));  // LSTAR
```

## Part 2 — 键盘无响应:AP 缺 LAPIC timer

### 现场

#DF 修好后 shell 能开,但**敲 USB 键盘不回显/不响应**,窗口仍是 shell 初始样子。鼠标点击能用(能开 shell)。

### 诊断(心跳 + 合成键注入)

键鼠走**同一队列**(`Mouse::event_queue`,键盘 `dispatch_key` 也双投递到此)+ 同一 pump(`cinux_poll_event`→`wm.handle_key`→焦点终端 `on_key`→shell stdin)。鼠标能用 → 链通 → 怀疑键盘事件没产出,或 pump 没跑。加探针:
- `[kbd-otc]`(USB 键盘 on_transfer_complete)、`[term-key]`(终端 on_key):终端**从不被调用**。
- gui_worker 心跳 `[gw-hb] cpu=`:gui_worker 在 **AP1**,只跑到 iter≈30(shell spawn 处)就**停**(<200 iter)。
- 合成键注入(自动 spawn 后往队列塞个 KeyDown):`[inj]` 触发了但 `[term-key]` 没触发 → pump 没跑。

→ **gui_worker 在 shell 起来后被饿死**(pin 在 AP1,AP1 停了)。

### 根因

- BSP 抢抢占靠 **PIT**(irq0);`switch_to_apic()`(irq_backend.cpp:81)把 PIT 的 IOAPIC 重定向目标设为 **`bsp`** → **PIT 只到 BSP**。
- LAPIC timer 寄存器有定义但**从没被编程**(`LocalAPIC::enable` 只设 SVR)。→ **AP1 没有任何周期定时器 → 无抢占**。
- shell `sys_read(stdin)` → `Pipe::read` 空读 **spin-`hlt`**(pipe.hpp 明说「真调度阻塞未实现」,用 spin-hlt 顶)。`hlt` 等 AP1 的周期中断唤醒 —— AP1 没有 → **AP1 死锁在 hlt**。
- gui_worker 被 pin 在 AP1 → 永不被调度 → **pump 不跑 → 键盘事件不派发**(鼠标偶尔能动是 shell 起来前 gui_worker 还跑了几帧)。
- 单核只有 BSP(有 PIT 抢占)→ shell spin-hlt 被 PIT 唤醒、与 gui_worker 交替 → 正常。

### 修法:AP 编程 LAPIC timer

给每个 AP 一个周期定时器驱动 `Scheduler::tick`,镜像 BSP 的 PIT 路径:
- [local_apic.hpp](../../kernel/drivers/apic/local_apic.hpp)/[.cpp](../../kernel/drivers/apic/local_apic.cpp):`setup_periodic_timer(vector, divide, init_count)` 写 DIVIDE/LVT_TIMER(周期,bit17)/INIT_COUNT。
- [smp.hpp](../../kernel/arch/x86_64/smp.hpp):`kLapicTimerVector=0x30`(避开 PIC 0x20-0x2F、xHCI 0x40、sigreturn 0x80、IPI 0xE0)。
- [interrupts.S](../../kernel/arch/x86_64/interrupts.S):`ISR_NOERRCODE lapic_timer_stub, lapic_timer_handler`(同 irq0_stub:handler 内 early EOI)。
- [irq_handlers.cpp](../../kernel/arch/x86_64/irq_handlers.cpp):irq_init 注册 0x30 stub 进共享 IDT。
- [ap_main.cpp](../../kernel/arch/x86_64/ap_main.cpp):`g_lapic.enable` 后 `setup_periodic_timer(0x30, /16, 1e6)`;`lapic_timer_handler` = `irq_eoi(0)` + `Scheduler::tick()`(early EOI 在 tick 的内联抢占切换之前,同 PIT)。

BSP 不动(继续用 PIT);LAPIC timer 只在 AP 上武装,BSP 上休眠。

## Part 3 — 加固:swapgs→sysretq 窗口 `cli`

诊断 #DF 时一度怀疑「swapgs→sysretq 两指令窗口被中断命中」(后证非本次根因),但该窗口确为真实潜在 SMP 隐患:swapgs 后仍 CPL=0 但 `GS_BASE` 已是用户值(0),窗口内命中可屏蔽中断 → ISR 入口看 saved CS=0x10 不 swapgs → handler 以 `percpu()==NULL` 跑。Linux 的 return-to-userpath 同样在此窗口保持 IF=0。给两处补 `cli`(sysretq 用 R11 恢复 IF,用户态中断不受影响):[usermode.S](../../kernel/arch/x86_64/usermode.S) `jump_to_usermode`、[syscall.S](../../kernel/arch/x86_64/syscall.S) `syscall_entry` 返回路径。

> 复现手段(诊断期临时,已撤):host_cinux render_frame 启动后 N 帧自动 `create_shell_terminal()` + 合成键注入,使 `make run`(-smp 2)无需手动点击即可触发。

## 验证

| 项 | 结果 |
|----|------|
| `run-kernel-test`(单核 KVM) | **931 passed, 0 failed** / ALL TESTS PASSED |
| `-smp 2` 启动 | AP1 online(LAPIC timer 武装)、desktop composited、**稳定无崩** |
| `-smp 2` 键盘链路(诊断期合成键) | gui_worker 持续 pump(770 iter/25s,修前 3);合成键送达终端 `on_key`;真键盘走同一队列 |
| 单核 Shell / 鼠标 | 回归正常 |

## GOTCHA 登记候选

- **GOTCHA(AP LSTAR)**:`usermode_init_asm` 设 STAR/SFMASK/EFER.SCE 但漏 LSTAR;只 BSP 的 `syscall_init` 写。AP `LSTAR==0` → 用户任务首个 `syscall` 跳地址 0 内核态 → #DF。AP 须对齐 BSP **全部** syscall MSR。
- **GOTCHA(AP 无定时器)**:PIT 的 IOAPIC 重定向只到 BSP;AP 必须自己武装 LAPIC timer(周期)做抢占,否则任务 spin-wait(如 shell 空 stdin)会死锁 AP、饿死同核任务。每核一个定时器是 SMP 标配。
- **GOTCHA(swapgs→sysretq 窗口)**:swapgs 后、sysretq 前仍 CPL=0 但 GS 已用户值;窗口需 `cli`,否则 -smp 下中断命中让 handler 以 `percpu()==NULL` 运行。

## 改动文件

- `kernel/arch/x86_64/ap_main.cpp` — AP 写 LSTAR;`lapic_timer_handler`(early EOI+tick);武装 LAPIC timer;+ `syscall_entry`/`irq_backend` 头
- `kernel/drivers/apic/local_apic.hpp`/`.cpp` — `setup_periodic_timer`
- `kernel/arch/x86_64/smp.hpp` — `kLapicTimerVector=0x30`
- `kernel/arch/x86_64/interrupts.S` — `lapic_timer_stub`(+ 注释)
- `kernel/arch/x86_64/irq_handlers.cpp` — 0x30 stub 声明 + irq_init 注册
- `kernel/arch/x86_64/usermode.S` / `syscall.S` — swapgs→sysretq 窗口 `cli` 加固

## Follow-up

- Pipe 仍 spin-hlt(「真调度阻塞未实现」)。LAPIC timer 让它在 AP 上能跑(定时器唤醒 spin),但本质是 busy-wait;后续应给 pipe 上 `schedule_blocked`+写端 wake(真阻塞 I/O),shell 空 stdin 时彻底让出 CPU。
- LAPIC timer 的 init_count(1e6,/16)按 QEMU 实测约几十 Hz 抢占、够用;未做对 PIT 的精校准,真硬件上频率不同可能需调。
- BSP 仍用 PIT 抢占;未来可统一改 LAPIC timer(去掉对 legacy PIT 的依赖)。
