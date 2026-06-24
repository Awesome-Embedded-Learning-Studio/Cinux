# F5-M5 USB 鼠标不动 → 中断投递根因链与修复 — 2026-06-24

> 分支 `feat/f5-m5-xhci-3`。本篇是 [2026-06-24-f5-m5-perf-mouse-handoff.md](2026-06-24-f5-m5-perf-mouse-handoff.md) 的**解决篇**:从「鼠标不跟随移动」一路挖到 4 个根因(共享一个总根),逐个修复并验证。前置的启动慢修复(yield + kResetIters)已在更早的 commit。
>
> **TL;DR**:鼠标不动不是 USB 的锅,是**键盘 IRQ1 的 EOI 走了 PIC(`PIC::send_eoi`),APIC 模式下是空操作 → 向量 0x21 永久卡在 LAPIC 的 ISR → 抬高 PPR → 堵死 PIT(0x20)及一切同/低优先级中断**。这一个 bug 同时导致了:PIT 冻结(= 启动慢的真因,前一个 AI 用 yield 压了症状没揪到)、MSI-X 投不到 CPU(鼠标不动)、偶尔卡死(协作式调度退化)。修复后连带挖出并修了 3 个 -smp 下的 AP 配置/调度 bug。

## 1. 诊断方法论(怎么定位的)

交接笔记已排除 GUI 渲染路径(`invalidate_cursor_move` 直接读 `Mouse::x()/y()`,不依赖事件出队),锁定**中断→事件投递链路**。本轮的关键探针:

1. **分层计数器**:在 gui_worker 周期打印 `xhci_irq`(MSI-X 到 CPU 次数)/ `mouse_reports`(on_transfer_complete 次数)/ `mx,my`。一次 `make run + 移动鼠标` 即可分层定位:
   - `rpt` 涨、`mx/my` 跟随 → 链路好,问题在 MSI-X 这一跳。
   - 实测:移动时 `rpt` 1→516、`mx/my` 精确跟随,**但 `irq` 恒为 1** → 事件链 100% 正常,MSI-X 没到 CPU。
2. **poll 对照实验**:gui_worker 每帧直接 `poll_events()`(与中断路径共用 `poll_events()` 之后的所有代码)。poll 能让鼠标动 → 证明链路好,缺的就是「中断到 CPU」。
3. **PIT 冻结副发现**:诊断时发现 `PIT::get_uptime()` 卡在 ~70ms(tick 7-16)再不涨——这跟 USB 无关,是个独立的大 bug,且是「启动慢」的**真因**(前一个 AI 量到 kernel_init 9 秒、归因 busy-poll,但真因是 PIT 死了 → 抢占式退化成协作式 → busy 的 usb::init 独占 CPU)。
4. **LAPIC 状态快照**:在 gui_worker 读 `TPR/PPR/SVR/ISR/IRR`。抓到决定性证据:
   ```
   TPR=0x0  PPR=0x20  SVR=0x1ff(已启用)  ISR20=0x2  IRR20=0x1001
   ```
   `ISR20=0x2` = bit1 = **向量 0x21(键盘 IRQ1)卡在 In-Service Register**;`PPR=0x20` 被它抬高;`IRR20=0x1001` = PIT(0x20)+mouse(0x2C)**都 pending 在 IRR 但投不出去**。

## 2. 根因 #1(总根):键盘 IRQ1 EOI 走了 PIC

### 现场

[keyboard.cpp](../../kernel/drivers/keyboard/keyboard.cpp) 的 `irq1_handler` 主路径和 usb_primary 路径用了 `PIC::send_eoi(1)`,而 `switch_to_apic()`(F4-M2)之后系统是 **APIC 模式,PIC 已 mask**——`PIC::send_eoi` 是**空操作**,清不掉 LAPIC 的 ISR。键盘中断触发一次 → 0x21 永久卡 ISR → PPR 抬到 0x20 → 堵死同优先级的 PIT(0x20)。

> F4-M2 当时「改了 5 处 EOI(pit/keyboard×2/mouse/irq_handlers×2)」,**偏偏漏了 keyboard 的主路径(269)和 usb_primary 路径(237)**——只有 EXTENDED 路径(243)用了 `irq_eoi`。这种「每个 handler 自己选 EOI 后端、还分两套 API」的设计本身就是漏的温床。

### 修复:EOI 下沉到 ISR stub(想忘也忘不了)

按用户要求做「激进、可交接」的架构修复——**EOI 收口到一处,handler 完全不碰**:

- 新增 `ISR_IRQ name, handler, irq` 宏([interrupts.S](../../kernel/arch/x86_64/interrupts.S)):call handler → `call irq_eoi_isr`(stub 统一 EOI)→ signal_check → epilogue。**所有设备 IRQ(keyboard/mouse/xhci/ipi/default)改用它,handler 删掉自己的 EOI**。新加中断再也不可能漏 EOI 或选错后端。
- 新增 `extern "C" void irq_eoi_isr(uint8_t)`([irq_handlers.cpp](../../kernel/arch/x86_64/irq_handlers.cpp))——APIC/PIC 后端在 `irq_eoi()` 一处决断。
- **PIT 是唯一例外**:用 `ISR_NOERRCODE` + handler 内**早 EOI**(`irq_eoi(0)` 在 `Scheduler::tick()` 之前)。原因:抢占式 `schedule()` 需 EOI 在切换之前(否则切走的任务再也收不到时钟 → 抢占退化成协作式)。文档化这个例外。

> **延迟抢占(deadline-preempt)尝试过又回退**:一开始为让 EOI 全在 stub(含 PIT)做了「tick() 只置 need_reschedule 标志、irq_exit() 在 EOI 后 schedule()」的延迟抢占。结果在 -smp + 鼠标轮询组合下触发 `irq0_stub` IRET 帧损坏 #GP(机制未完全定位)。回退到 option B(PIT 早 EOI + 内联 schedule),稳。

## 3. 根因 #2:AP 没设 CR4.OSFXSR → fxsave #UD

-smp 下 AP 启动后 `#UD(Invalid Opcode)`。解码 RIP 落在 fxsave/fxrstor。查 [ap_trampoline.S](../../kernel/arch/x86_64/ap_trampoline.S):只设了 `CR4.PAE(0x20)`,**没设 OSFXSR/OSXMMEXCPT**;而 scheduler 每次 context switch 都 `fxsave/fxrstor`,CR4.OSFXSR=0 时这俩指令 `#UD`。BSP 在 [boot.S](../../kernel/arch/x86_64/boot.S) 设了,AP 没跟上。

**修复**:trampoline 的 CR4 写 `0x620`(PAE|OSFXSR|OSXMMEXCPT),与 BSP 一致。

## 4. MSI-X 在 QEMU/nested-KVM 下投递失效 → 鼠标改轮询

MSI-X 路径审下来武装正确(LAPIC 地址 0xFEE00000、vector 0x40、entry unmask、enable),但 `IMAN.IE` **死活锁存不上**:试了 4 种写法(直写 / mask IP / 先清 IP / poll_events 排干后写),`IMAN.IE` 始终读回 0,只有偶发 1 次 MSI(`irq=1`)。这是 QEMU xHCI + WSL2 nested-KVM 的 MSI-X 虚拟化问题,非 guest 代码能简单修。

按「可用优先」,**鼠标改用 gui_worker 每帧 `poll_events()` 服务事件环**([init.cpp](../../kernel/proc/init.cpp))——合法设计(很多 USB 栈轮询),可靠、不依赖 MSI-X 虚拟化。MSI-X 武装代码保留(真实 HW 上是对的)。验证:移动鼠标 `rpt` 1→516、`mx/my` 全程跟随、光标动。

## 5. 根因 #3:AP 没设 EFER.SCE/STAR → shell sysretq #UD

鼠标修好后用户点 Shell 图标:`execve /bin/sh` → jump_to_usermode → **`#UD` @ `sysretq`**(解码 RIP=0x42C5 正是 sysretq)。SYSRETQ 在 EFER.SCE=0 时 #UD。

查 [ap_main.cpp](../../kernel/arch/x86_64/ap_main.cpp):AP 只设了 GS base,**没调 `usermode_init_asm`**(设 STAR/SFMASK/EFER.SCE)。BSP 在 `usermode_init()` 里设了,AP 没跟上(与 OSFXSR 同类——AP 没对齐 BSP 的 CPU 配置)。用户任务迁移到 AP 做 sysretq → AP 上 EFER.SCE=0 → #UD。

**修复**:ap_main 在 GDT/IDT 之后调 `usermode_init_asm()`,AP 的 syscall MSR 与 BSP 一致。

## 6. 根因 #4:deferred-free 跨核 reap 竞态 → kernel_init-exit #GP

EFER.SCE 修复后,-smp 2 启动**偶发** `#GP` @ `kernel_init exited` 之后(RIP=0x282, CS=0x0000,坏 IRET 帧)。单 CPU 干净 → SMP 竞态。连跑 8 次约 1 次中。

定位 [scheduler.cpp](../../kernel/proc/scheduler.cpp) `exit_current()`:**第 253 行 `enqueue_deferred(prev)` 把 kernel_init 放进全局 deferred 链表,然后 272-274 行还在 `fxsave(prev->fpu_state)` / `context_switch(&prev->ctx,…)` 用着 prev**。而 AP 醒来(被 resched IPI 唤醒)跑 `schedule()→reap_deferred()` 拿**全局**链表,可能在 BSP 还在切换时把 kernel_init **free 掉** → use-after-free → ctx 损坏 → #GP。

**修复**:deferred 链表改**每 CPU**(`g_deferred_heads[kMaxCpus]`,按 `percpu()->cpu_id` 索引)。BSP defer 的只 BSP reap(在 BSP 自己的下一次 schedule(),此时 exit_current 的切换早已完成);AP 只 reap 自己的(空)。从结构上消灭跨核 mid-switch free。验证:**修复后 -smp 2 连跑 8 次全干净,#GP=0**。

## 7. 默认 -smp 2

[qemu.cmake](../../cmake/qemu.cmake) `run` target 加 `-smp 2`(用户决策)。上述 AP 修复(OSFXSR / EFER.SCE / deferred)都是 -smp 才暴露的——单 CPU 全藏住了。

## 8. 验证矩阵

| 项 | 结果 |
|----|------|
| `run-kernel-test`(KVM) | **922 + 931 passed, 0 failed** |
| `run-kernel-test`(TCG,无 KVM 时) | 931/0 |
| `-smp 2` 启动压测 ×8 | **全干净,#GP=0 / #UD=0**(deferred 修复前 ~1/6 #GP) |
| 鼠标移动 | `rpt` 跟随、`mx/my` 精确、光标动(poll) |
| LAPIC 状态(修复后) | `TPR=0 PPR=0 ISR20=0 IRR20=0` —— 全清(修复前 0x21 卡死) |

> 待用户确认:点 Shell 图标(EFER.SCE 修复)—— boot 期 #UD=0,Shell 的 sysretq #UD 只在点击触发,需 GUI 交互验证。

## 9. GOTCHA 登记候选

- **GOTCHA(keyboard EOI)**:APIC 模式下 `PIC::send_eoi` 是空操作。F4-M2 改 EOI 漏了 keyboard 主路径 → 0x21 卡 LAPIC ISR → 堵死 PIT 及低优先级中断。教训:EOI 后端选择不该散在每个 handler。→ 已用 ISR_IRQ stub 根除。
- **GOTCHA(AP CR4.OSFXSR)**:AP trampoline 只设 PAE,scheduler 的 fxsave/fxrstor 在 AP 上 #UD。AP 必须对齐 BSP 的 CR4。
- **GOTCHA(AP EFER.SCE)**:AP 没设 EFER.SCE/STAR/SFMASK,用户任务迁移到 AP 后 sysretq #UD。AP 必须调 usermode_init_asm。
- **GOTCHA(deferred-free SMP)**:exit_current 先 enqueue_deferred 再 context_switch 用 prev,跨核 reap 会 mid-switch free。→ 改每 CPU 链表。
- **GOTCHA(MSI-X IE 不锁存)**:QEMU xHCI + nested-KVM 下 IMAN.IE 写不上去(试了 4 法),production 靠轮询事件环。

## 10. 改动文件

- `kernel/arch/x86_64/interrupts.S` — `ISR_IRQ` 宏 + IRQ stub 迁移;PIT 留 ISR_NOERRCODE
- `kernel/arch/x86_64/irq_handlers.cpp` — `irq_eoi_isr` extern "C";default/mouse/xhci stub 删 EOI
- `kernel/arch/x86_64/ap_trampoline.S` — CR4 加 OSFXSR/OSXMMEXCPT
- `kernel/arch/x86_64/ap_main.cpp` — 调 `usermode_init_asm`(EFER.SCE/STAR)
- `kernel/proc/scheduler.cpp` — deferred 链表改每 CPU
- `kernel/drivers/keyboard/keyboard.cpp` — 删 EOI(stub 接管)
- `kernel/drivers/mouse/mouse.cpp` / `usb_mouse.cpp` — 删 EOI
- `kernel/drivers/pit/pit.cpp` — handler 内早 `irq_eoi(0)` + `Scheduler::tick()`
- `kernel/drivers/usb/xhci_irq.cpp` / `xhci_controller.cpp` / `xhci_controller.hpp` — 删 handler EOI;`has_controller()` 探针
- `kernel/proc/init.cpp` — gui_worker 每帧 `poll_events()`(鼠标事件服务)
- `cmake/qemu.cmake` — `run` 默认 `-smp 2`

## 11. Follow-up

- MSI-X 在 nested-KVM 的 IE 不锁存:留待真实 HW / 未来 QEMU 验证;production 走轮询。
- 延迟抢占(让 PIT EOI 也进 stub):尝试过因 #GP 回退,机制待查(suspect:从 irq_exit 调 schedule 与 poll 的交互);当前 option B(PIT 早 EOI 例外)足够稳。
- `-smp` 其它残留竞态(memory 提到的 TLB shootdown / registry TOCTOU / reparent)与本批无关,留 F4 follow-up。
