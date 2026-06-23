# F13 visor — 重构与代码分离执行计划(DRAFT v2)

> ⚠️ **DRAFT / 草稿(2026-06-21)**。「具体如何重构 GUI 底层 + 如何分离代码」的执行计划。配套:[visor-01-presets.md](visor-01-presets.md)、[architecture](../../notes/2026-06-21-f13-visor-architecture.md) / [roadmap](../../notes/2026-06-21-f13-visor-roadmap.md) 笔记。
>
> **v2 修订(2026-06-21)**:吸收外部审查 `review.md`——**PIT-IRQ-composite 反转提前到绘制抽象之前**("调度模型先正确,绘制抽象后正确",审查 S1.4)。PIT 反转用现有 Canvas/WindowManager(不依赖 visor ABI 空壳),行为不变去 IRQ composite。原顺序 spawn→骨架→绘制→PAT 调整为 **spawn(✓)→ PIT 反转 → Host ABI → dirty → SwRaster → Surface/Widget**。
>
> **核心策略**:先在 CinuxOS 仓库内重构(改形状),再物理分离(visor 独立仓库 submodule)。**绝不一次性大爆炸**——每步独立成批 + `timeout 40` QEMU 验证。

## §0 两条主线:重构(改形状)vs 分离(挪位置)

| | 重构(refactor) | 分离(separation) |
|---|---|---|
| 目标 | GUI 底层长成 visor 形状(Host ABI 表 / 后端可插拔 / pump / dirty-region / static-only) | 平台无关 core 物理挪到独立 visor 仓库 |
| 在哪做 | **CinuxOS 仓库内** | visor 仓库(submodule) |
| 何时 | 先做(每步可验证) | L4a core 可独立时(M1 后) |
| 风险 | 中(碰 spawn/PIT,易回归) | 低(文件移动 + 构建接线) |

**铁律**:重构期 visor core 物理留 CinuxOS 仓库内(`kernel/gui/visor_core/`),先验证形状对,再分离。**别在形状没验证前挪仓库**。

## §1 步骤 0:spawn 公共化前置 ✅ 已完成(commit `82e9023`)

**已完成**:提取 `launch_user_program(path, argv, envp)`(execve + 用户栈 + Stack VMA + jump)到 [user_launch.{hpp,cpp}](../../kernel/proc/user_launch.hpp);init.cpp / gui_init.cpp 两处重复收敛。run-kernel-test 887/0 + GUI 冒烟无 panic。详见 [笔记](../../notes/2026-06-21-f13-visor-spawn-launch.md)。

(原计划还列了"处理 GOTCHA#22 tid 污染 + F2-M5 VMA 三处合一",实测 Stack VMA 实际只两处 init+gui_init,fork/clone 继承父 VMA 不动;GOTCHA#22 是测试基础设施问题,与本批正交,留 follow-up。)

## §2 步骤 1:PIT-IRQ-composite 反转(v2 提前,审查 S1.4)★★★ 最危险的可靠性债,先于绘制抽象

**为什么提前**:当前 [gui_tick_callback](../../kernel/gui/gui_init.cpp#L260) 跑在 **PIT IRQ0** 里做 dequeue + terminal poll + render + 全屏 composite,且 [gui_init.cpp:355-358](../../kernel/gui/gui_init.cpp#L355-L358) 注释承认 **production 只收到 1 个 PIT tick**,靠 `wm.composite()` 预绘 workaround 掩盖。这比绘制抽象危险得多——是黑屏/卡死的直接来源。**调度模型先正确,绘制抽象后正确**。

**动作(用现有 Canvas/WindowManager,不依赖 visor ABI 空壳)**:
1. 把 [gui_worker_thread](../../kernel/proc/init.cpp)(现仅 `gui_process_pending + yield`)升级为循环调 **`gui_pump()` + yield**。
2. `gui_pump()`(新建,先在 gui_init.cpp 或 proc 内,非 visor):drain Mouse EventQueue → 分发 WM → poll terminal → `wm.composite()`。即把现 IRQ 里的逻辑搬到 worker 线程。
3. **退役 `gui_tick_callback` 在 PIT IRQ0 里的 composite**(ISR 改为仅 enqueue 输入,或彻底退役 tick callback)。
4. worker 优先级在 F3 调度器侧拉到与 shell 同级或更高。
5. **硬验收**:即使 PIT tick 完全不送达,GUI 仍持续刷新(靠 `Scheduler::yield` + `now_ms` 节流)——把可靠性债转成正向验收点。

**验证**:`timeout 40 run-kernel-test` 绿 + `timeout 40 cmake --build build --target run` 冒烟(鼠键响应 + composite 正常 + **断言 pump 跑在内核线程而非 IRQ0**)。**这一步行为不变(逻辑只是搬家),是后续一切 visor 抽象的稳定地基**。

## §3 步骤 2:visor 形状骨架(Cinux 仓库内,先不挪 submodule)

在 `kernel/gui/visor_core/`(暂留 Cinux 仓库)落 visor 形状:

1. `visor_host.h`(L0):**核心表 + Desktop extension**(见 [presets §4 v2](visor-01-presets.md),非旧 5 大表)+ `visor_event` 定宽头 + `static_assert`。
2. `visor_conf.h`:3 profile 预设 + `VISOR_*` 宏(见 [presets §2 v2](visor-01-presets.md))。
3. `visor_pump()` 骨架(L4a):把 §2 的 `gui_pump` 升级为 `visor_pump(now_ms)`(经 Host ABI 表)。
4. Cinux host adapter(`kernel/gui/visor_host_cinux.cpp`):填核心表(flush 转发 Canvas+flip / poll_event 转发 Mouse 队列+keyboard / now_ms / alloc=kmalloc)+ Desktop extension(spawn 转发 §1 launch_user_program 的上层)。
5. CMake:`visor_core` 作 big_kernel_common 一部分(CINUX_GUI 守卫)。

**验证**:全量编译绿;`cmake --build build --target run` GUI 行为与重构前一致(adapter 转发,行为不变)。

## §4 步骤 3:绘制引擎接管(M1)+ dirty-region

**M1 绘制引擎**:
1. L3 SwRaster:`fill_rect/blit/blit_blend(Q8.8 定点)/glyph_blit/draw_line` + clip-rect + transform 状态栈。从 [canvas.hpp](../../kernel/drivers/canvas.hpp) 的 draw_* 泛化。
2. 像素格式枚举 + **硬契约**(stride/endianness/premultiplied alpha/byte-bit order,见 [presets §4 像素格式 v2](visor-01-presets.md))。
3. **Region 一等**(intersect/union/subtract/translate + 退化策略)+ dirty-region invalidation。
4. **dirty lowering**(v2):Desktop rect occlusion;MCU 合并 page band;彩屏 tile。
5. 废弃 [canvas.hpp:167-168](../../kernel/drivers/canvas.hpp#L167-L168) colorkey,改真 alpha(独立 1bpp mask);icon 资源重导出。

> GPU:本步**只交付纯软件 SwRaster**。GPU 走 texture compositor(M7),不在 M1 定 primitive CommandBuffer(审查 S1.2)。

**验证**:run-kernel-test 绿 + 冒烟;alpha 定点单测;dirty flush 只报脏区(非全屏)。

## §5 步骤 4:窗口系统 + 控件解耦(M4/M5)

1. **M4 合成器**:Surface 四态协议(attach/damage/commit/release);[window_manager.cpp](../../kernel/gui/window_manager.cpp) 全屏 composite → dirty-region 合成;Decorator paint/hit_test 同实例。**v2:四态保留(ownership 抽象对),但不早期 Wayland 化**——z-order/role/IPC/fence/错误模型不进 M4,字段预留 IPC 兼容即可(审查 S1.1)。
2. **M5 Window vs Widget 解耦**:Window(顶层 Surface)与 Widget(子树,共享父窗口 backing store)分离;[Terminal](../../kernel/gui/terminal.cpp) 从继承 Window 改为 Window 内根 Widget;`VISOR_USE_TERMINAL`;首批 Label/Button/Slider;**input capture/focus/grab**(v2,审查 S2.1)+ a11y/UTF-8 hook Day-1 预留。

**验证**:多窗口拖动/关闭/焦点一致;pointer capture(拖窗/弹窗);Terminal 作根 Widget 正常。

## §6 代码物理分离机制(submodule + 重编译,对齐 Cinux-Base)

**时机**:M1(core 绘制引擎形状验证)后才物理分离。此前 core 留 `kernel/gui/visor_core/`。

**机制**(对齐 [third_party/Cinux-Base](../../third_party/Cinux-Base)):
1. 建 visor 独立仓库(用户单独开发),`third_party/visor` submodule。
2. **CinuxOS CMake 用 `add_library(visor_core STATIC <core 源>)` 重新编译**——内核 include + `-fno-exceptions -fno-rtti -ffreestanding` + kmalloc allocator。**不链 visor 自建 `.a`**(三套工具链各异,跟 Cinux-Base 一样)。
3. host SDL simulator 与 MCU 各自独立构建。
4. profile 由 CinuxOS 侧注入(`-DVISOR_PROFILE=DESKTOP`)。

## §7 剥离顺序(v2 调整:PIT 反转提前)

```
spawn 公共化 ✅ → PIT 反转(worker pump,行为不变) → L0 Host ABI 表
   → L3 SwRaster + Region/dirty → L4a core(pump) → L5 合成器 → L6 控件
```

每层独立成批:接口 + adapter 转发(行为不变)→ core 接管 → 验证 → 下一层。**每批 `timeout 40` QEMU 验证**。

## §8 每步验证策略(对齐 DIRECTIVES L5)

- 内核改动:`timeout 40 cmake --build build --target run-kernel-test -j$(nproc)`(绿才提交)。
- GUI 行为:`timeout 40 cmake --build build --target run` 冒烟(鼠键/窗口/shell 与重构前一致)。注:CLAUDE.md 写「make run」,实际 target 是 cmake 的 `run`。
- 改公共接口/mock:`cmake --build build -j$(nproc)` 全量(CI 盲区:run-kernel-test 不编 test/unit/)。
- visor core 跨平台:双构建 + nm 门禁 + golden image/pixel CRC(见 [presets §5 v2](visor-01-presets.md))。
- 一批一 commit,绿才提交;commit msg `<type>(gui): <中文简述>`,不带 AI 署名。

## §9 回归风险点(v2:PIT 反转提前到最高优先)

| 风险点 | 为什么险 | 缓解 |
|--------|---------|------|
| **PIT 反转(§2)★★★** | 改 GUI 驱动模型,黑屏/卡死/输入饥饿/terminal poll 阻塞/worker 优先级不够 | 硬验收 PIT 不送达仍刷新;wake/deadline/frame budget;worker 优先级;timeout 40 |
| spawn 公共化(§1)✅ | 碰 execve/PF/AddressSpace/F2-M5 VMA | 已完成 887/0 |
| Surface 四态(§5) | buffer ownership/时序 | 进程内 commit 后 swap;**不早期 Wayland 化** |
| static-only core(§3/§6) | 现有全 new | grep + CI 门禁 core/ 禁 new/std::vector |
| colorkey→alpha(§4) | 语义破坏 | icon 重导出带 alpha mask;M1 定像素格式硬契约 |
| dirty lowering(§4) | MCU 小 rect = SPI command 风暴 | page-band/tile lowering(F1/彩屏) |

## §10 明确不做(v2 加 GPU/Wayland 不早期定)

- 不在 M0-M5 做完整桌面控件库(L4b / M9 长弧)。
- 不在重构期做用户态 GUI server(M8)。
- **不在 M1 定 GPU primitive CommandBuffer**(v2):GPU 先 texture compositor(M7),有真 DMA2D/GLES 再抽 draw-list(审查 S1.2)。
- **不早期 Wayland 化**(v2):Surface 四态保留,但 z-order/role/IPC/fence 不进 M4(审查 S1.1)。
- 不引 libdrm/KMS 真依赖(内核态用不上,只对齐数据形状)。
- 不一次性大爆炸(每步独立可验证)。

---

> **DRAFT v2 备注**:下一批 = §2 PIT 反转(不依赖 visor 仓库,现在就能做,且是最危险的可靠性债)。§3 visor 骨架等 PIT 反转行为不变后再上。
