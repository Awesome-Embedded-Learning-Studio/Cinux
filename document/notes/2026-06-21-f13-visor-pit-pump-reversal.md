# F13 visor — PIT-IRQ-composite 反转(2026-06-21)

> F13 visor 第二批代码。[visor-02 §2](../todo/f13-gui/visor-02-refactor-and-separation.md)(M1)。把 GUI 刷新从 PIT IRQ0 callback 移到 worker 线程 pump 循环。分支 `feat/f13-visor`。行为重构(逻辑搬家),消除 IRQ 跑渲染反模式 + 顺修「APIC 只送 1 PIT tick」可靠性债。

## 背景

Cinux GUI 的 composite 跑在 **PIT IRQ0**([gui_tick_callback](../../kernel/gui/gui_init.cpp)),且 production APIC 只送 1 PIT tick(旧注释承认),靠 `wm.composite()` 预绘 workaround 掩盖。IRQ 跑渲染是雷区(外部审查 S1.4):黑屏/卡死直接来源,比绘制抽象危险。

## 目标

GUI 刷新从「PIT IRQ callback 驱动」改「worker 线程 pump 循环驱动」。行为不变(逻辑搬家),消除 IRQ composite + 不依赖 PIT tick 可靠性。

## 设计

- `gui_tick_callback`(IRQ0)→ **`gui_pump`**(worker 线程):drain Mouse 事件 → WM handle → consume icon action → `create_shell_terminal`(同线程)→ poll terminal → composite。
- `gui_worker_thread`([init.cpp](../../kernel/proc/init.cpp)):循环 `gui_pump() + Scheduler::yield`(取代 `gui_process_pending + yield`)。
- `gui_start`:**退役 `PIT::set_tick_callback`**(不再注册 GUI callback);保留 Mouse::init + icons + composite 一次(首次显示)。
- 删 `g_pending_action` 原子中转(反转后 icon action 同线程处理,不需 ISR→worker 跨上下文原子)。
- **`Scheduler::tick()` 独立**:`PIT::irq0_handler` 调 `invoke_tick_callback(GUI)` + `Scheduler::tick()`(调度)。退役 GUI callback 只让 invoke 变 no-op,**Scheduler::tick 继续**(调度时间片不破)。

## 验证(全绿)

- 编译零警告(big_kernel_test + big_kernel);clang-format 过;顺带清掉 6 个 IWYU 冗余 include(paging/usermode/pit/atomic/pmm,符号都在 user_launch.cpp 或不再用)。
- **run-kernel-test 887/0**(回归不破;test_gui_integration 的 tick_callback 测试**模拟逻辑、不调函数**,反转不影响)。
- **production GUI 冒烟**:`GUI subsystem initialised (refresh via gui_worker pump)` → `desktop composited; refresh driven by gui_worker pump loop` → `gui_worker (tid=4) added to RoundRobin` → `Worker thread started (drives gui_pump refresh loop)`。**无** `tick callback registered` / `first composite tick`(已退役)。无 panic。

## GOTCHA

- **测试不破**:[test_gui_integration](../../kernel/test/test_gui_integration.cpp) 的 `test_tick_callback_*` 自己复制 drain+dispatch 逻辑(不调 gui_tick_callback 函数),反转后它们测的 WM 事件处理逻辑不变。
- **Scheduler::tick 独立于 GUI callback**:退役 GUI callback 前,确认 PIT irq0_handler 的 `Scheduler::tick()`(调度时间片)与 `invoke_tick_callback`(GUI)是两条独立调用——退役一个不破另一个。
- **APIC 1-tick 债转正向验收**:production 即使 PIT callback 不送达(旧债),worker 自己循环 pump,GUI 仍刷新——可靠性从「依赖 PIT tick」转为「依赖 worker 被调度」。

## follow-up

- **worker busy loop**:`pump + yield` 持续,可能占 CPU。审查建议 wake/deadline/frame budget(pump 内 `now_ms` 节流 composite 到 60fps,无事件时 wait)。初版不节流(行为不变优先),冒烟 OK;优化留 follow-up。
- **worker 优先级**:审查建议拉到与 shell 同级。初版默认优先级,冒烟 OK;优化 follow-up。

## 下一步

[visor-02 §3](../todo/f13-gui/visor-02-refactor-and-separation.md):visor 形状骨架(`kernel/gui/visor_core/` + `visor_host.h` 核心表+Desktop extension + Cinux adapter 填表转发)。ABI 契约批(`visor_host.h` 签名 v2 已在 [presets §4](../todo/f13-gui/visor-01-presets.md) 定),建新子系统骨架。
