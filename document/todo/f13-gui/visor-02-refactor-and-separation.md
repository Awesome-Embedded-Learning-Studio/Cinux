# F13 visor — 重构与代码分离执行计划(DRAFT)

> ⚠️ **DRAFT / 草稿(2026-06-21)**。这是「具体如何重构 GUI 底层 + 如何分离代码」的执行计划。未实现验证,方向可能调整。配套:[visor-01-presets.md](visor-01-presets.md)、[architecture 笔记](../../notes/2026-06-21-f13-visor-architecture.md)、[roadmap 笔记](../../notes/2026-06-21-f13-visor-roadmap.md)。
>
> **核心策略**:先在 CinuxOS 仓库内重构(把 GUI 底层改成 visor 形状),再物理分离(visor 独立仓库 submodule)。**绝不一次性大爆炸**——每步独立成批 + `timeout 40` QEMU 验证(run-kernel-test 绿 + make run 冒烟)。

## §0 两条主线:重构(改形状)vs 分离(挪位置)

| | 重构(refactor) | 分离(separation) |
|---|---|---|
| 目标 | 让 GUI 底层长成 visor 七层形状(Host ABI 表 / 后端可插拔 / pump / dirty-region / static-only) | 把平台无关 core 物理挪到独立 visor 仓库 |
| 在哪做 | **CinuxOS 仓库内**(改 [kernel/gui/](../../kernel/gui/) + [kernel/drivers/](../../kernel/drivers/)) | visor 仓库(submodule) |
| 何时 | 先做(每步可验证) | 重构到 L4a core 可独立时(M1 后)才做 |
| 风险 | 中(碰启动路径 spawn / PIT,易回归) | 低(纯文件移动 + 构建接线) |

**铁律**:重构期 visor core 仍物理在 CinuxOS 仓库内(比如 `kernel/gui/visor_core/`),先验证形状对,再 M1 后物理分离到 submodule。**别在形状没验证前就挪仓库**。

## §1 步骤 0:spawn 公共化前置(M0 前置,Cinux 仓库,独立 PR)

**为什么先做**:[gui_init.cpp:94-159](../../kernel/gui/gui_init.cpp#L94-L159) 的 shell 启动 与 [init.cpp:81-140](../../kernel/proc/init.cpp#L81-L140) 的 fork 路径**高度重复**(同 USER_STACK_PAGES 循环 + 同 kStackVma 插入)。不先合并,L7 adapter `spawn` 就是在重写,且是迁移最易回归点。

**动作**:
1. 提取公共 `spawn_user_process(path, argv, envp, stdin_inode, stdout_inode) -> Task*` 到 `kernel/proc/`(复用 fork→execve 公共路径,对齐 Linux,不重写)。
2. gui_init.cpp 的 `shell_child_entry` + `create_shell_terminal` 改调公共函数,删内联(AddressSpace/g_pmm.alloc_page/USER_STACK_PAGES 循环/Stack VMA 插入/FDTable set/pipe/execve/jump_to_usermode)。
3. 处理 TaskBuilder 全局 tid 污染(GOTCHA#22)。
4. F2-M5 Stack VMA 硬门控三处合一(现在 init.cpp / gui_init.cpp / fork 各一处)。

**验证**:`timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` 绿 + `timeout 40 make run` 冒烟(shell 起得来、输入输出正常、行为与重构前一致)。**这步是 visor 一切的前提,单独成批 + commit**。

## §2 步骤 1:visor 形状骨架(Cinux 仓库内,先不挪 submodule)

在 `kernel/gui/visor_core/`(暂留 Cinux 仓库)落 visor 形状:

1. `visor_host.h`(L0):5 张函数指针表 + `visor_event` 定宽头 + `static_assert`(见 [presets §4](visor-01-presets.md))。
2. `visor_conf.h`:4 profile 预设 + `VISOR_*` 宏(见 [presets §2](visor-01-presets.md))。
3. `visor_pump()` 空骨架(L4a):先空实现,`visor_init(host)` 只存表。
4. Cinux host adapter(L7,`kernel/gui/visor_host_cinux.cpp`):填 Display 表转发 Canvas+flip、Input 表转发 Mouse 队列+keyboard、Time 表 now_ms、Process 表转发步骤 0 的公共 spawn。
5. CMake:`visor_core` 作 big_kernel_common 的一部分编译(CINUX_GUI 守卫)。

**验证**:全量编译绿;`make run` GUI 行为与重构前一致(adapter 转发,行为不变)。此时 visor 是「空壳 + 转发」,还没真正接管渲染。

## §3 步骤 2:绘制引擎接管(M1)+ PIT 反转(M3)

**M1 绘制引擎**:
1. L3 SwRaster:`fill_rect/blit/blit_blend(Q8.8 定点)/glyph_blit/draw_line` + clip-rect + transform 状态栈。从 [canvas.hpp](../../kernel/drivers/canvas.hpp) 的 draw_* 泛化而来。
2. 像素格式枚举(XRGB8888/RGB565/1bpp),Color 统一 RGB888 落盘转换。
3. 脏区管理(invalidation-area 队列,单脏矩形 + page-align 最小版)。
4. 废弃 [canvas.hpp:167-168](../../kernel/drivers/canvas.hpp#L167-L168) colorkey,改真 alpha(独立 1bpp mask);icon 资源重导出。

**M3 PIT-IRQ-composite 反转**(最关键的行为重构):
1. 把 [gui_worker_thread](../../kernel/proc/init.cpp) 从「只 gui_process_pending + yield」升级为「循环调 `visor_pump(now_ms)` + yield」。
2. pump 内部:poll 输入 → 事件分发 → 脏区重绘 → flush(composite)。
3. **退役 [gui_tick_callback](../../kernel/gui/gui_init.cpp#L260) 在 PIT IRQ0 里的 composite**(ISR 改为仅 enqueue 输入,或彻底退役)。
4. worker 优先级在 F3 调度器侧拉到与 shell 同级或更高。
5. **硬验收**:即使 PIT tick 完全不送达(「APIC 只送 1 tick」债),GUI 仍持续刷新(靠 Scheduler::yield + now_ms 节流)。

**验证**:run-kernel-test 绿 + make run 冒烟(鼠键响应 + composite 正常);断言 pump 跑在内核线程而非 IRQ0(打印/断言)。

## §4 步骤 3:窗口系统 + 控件解耦(M4/M5)

1. **M4 合成器**:Surface 四态协议(attach/damage/commit/release);[window_manager.cpp](../../kernel/gui/window_manager.cpp) 全屏 composite → dirty-region 合成;Decorator paint/hit_test 同实例。
2. **M5 Window vs Widget 解耦**:Window(顶层 Surface)与 Widget(矩形子树,共享父窗口 backing store)分离;[Terminal](../../kernel/gui/terminal.cpp) 从继承 Window 改为 Window 内根 Widget;加 `VISOR_USE_TERMINAL` 宏;首批 Label/Button/Slider;a11y/UTF-8 hook Day-1 预留。

**验证**:多窗口拖动/关闭/焦点一致;Terminal 作根 Widget 正常;flush 只报脏矩形。

## §5 代码物理分离机制(submodule + 重编译,对齐 Cinux-Base)

**时机**:M1(core 绘制引擎形状验证通过)后才物理分离。此前 core 留 `kernel/gui/visor_core/`。

**机制**(对齐已有 [third_party/Cinux-Base](../../third_party/Cinux-Base) 子模块范式):
1. 建 visor 独立仓库(用户单独开发),`third_party/visor` submodule 挂进 CinuxOS。
2. **CinuxOS CMake 用 `add_library(visor_core STATIC <visor core 源>)` 重新编译**——带内核 include + `-fno-exceptions -fno-rtti -ffreestanding` + kmalloc allocator。**不链接 visor 自建 `.a`**(visor 的 .a 是 host gcc 编的,内核要内核工具链 + freestanding 重编,跟 Cinux-Base 一样)。
3. host SDL simulator 与 MCU 各自独立构建(visor 仓库自己的 CMake,target 不同)。
4. profile 由 CinuxOS 侧编译定义注入(`-DVISOR_PROFILE=DESKTOP` 等)。

> 关键:visor core 源码一份,但**三套工具链各自重编**(内核 freestanding / host gcc / arm-none-eabi-gcc)。不是链预编译库。

## §6 分层剥离顺序(依赖驱动,从下到上)

```
L0 Host ABI 表  →  L1 DisplayBackend  →  L3 SwRaster  →  L4a core(脏区/pump)
                                                              ↓
                                                    L5 合成器  →  L6 控件
```

每层剥离独立成批:先建接口 + adapter 转发(行为不变),再让 core 真正接管,验证后下一层。**每批 `timeout 40` QEMU 验证**。

## §7 每步验证策略(对齐 DIRECTIVES L5)

- 内核改动:`timeout 40 cmake --build build --target run-kernel-test -j$(nproc)`(绿才提交)。
- GUI 行为:`timeout 40 make run` 冒烟(鼠键/窗口/shell 输入输出与重构前一致)。
- 改公共接口/mock:`cmake --build build -j$(nproc)` 全量(CI 盲区:run-kernel-test 不编 test/unit/)。
- visor core 跨平台:双构建(host gcc + arm-none-eabi-gcc)+ nm 门禁。
- 一批一 commit,绿才提交;commit msg `<type>(gui): <中文简述>`,不带 AI 署名。

## §8 回归风险点(重点关注)

| 风险点 | 为什么险 | 缓解 |
|--------|---------|------|
| spawn 公共化(§1) | 碰 execve/PF/AddressSpace/F2-M5 VMA,启动路径高危 | 单独成批 + QEMU 冒烟;对齐 Linux 不重写 |
| PIT 反转(§3) | 改 GUI 驱动模型,可能黑屏/卡死 | 硬验收 PIT 不送达仍刷新;worker 优先级;timeout 40 |
| Surface 四态(§4) | buffer 所有权/时序,进程内隐式 vs 显式 | Day-1 显式四态,进程内 commit 后 swap 隐藏所有权 |
| static-only core(§2/§5) | 现有全 new,改零 new 易漏 | grep + CI 门禁 core/ 禁 new/std::vector |
| colorkey→alpha(§3) | 语义破坏,纯黑像素 | icon 资源重导出带 alpha mask;M1 定数据格式 |

## §9 明确不做(防止 scope 蔓延)

- 不在 M0-M5 做完整桌面控件库(那是 L4b / M9 长弧)。
- 不在重构期做用户态 GUI server(M8,需共享内存 syscall / evdev)。
- 不引 libdrm/KMS 真依赖(Cinux 内核态用不上,只对齐数据形状)。
- 不为 STM32F1 牺牲桌面体验(profile ceiling,各自最优)。
- 不一次性大爆炸重构(每步独立可验证)。

> **DRAFT 备注**:步骤编号与边界以实际重构推进时订正;§1 spawn 公共化是确定的第一个动作(不依赖 visor 仓库),可作为 F13 第一批启动。
