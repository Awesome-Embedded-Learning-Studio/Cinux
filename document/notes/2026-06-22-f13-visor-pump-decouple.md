# F13 visor — pump 解耦为 host-neutral(分离前 Step 1)

> 2026-06-22 · commit `8a4add4` · run-kernel-test **930 → 928/0** · 分支 `feat/f13-visor`(未 push)

## 动机(用户决策)

用户要在另一项目并行启动 SDL 模拟 + F1~F4 MCU 实操,需尽快把 visor core 解耦成**真正
跨平台的独立 GUI 框架**。原则:**visor core 越薄越 host-neutral 越好,Cinux 这边感知越少越好,
越简单越好**(另一边会大幅度重写实现)。「等第二个 host 再分」有鸡蛋问题——SDL/MCU 正是
要消费分离 core 的 host,故**分离必须在那些 host 之前**。submodule 边界本身是逼出最后一段
解耦的强制函数。

## 做了什么:visor_pump 零 cinux include

`visor_pump.cpp` 从「直接调 cinux WindowManager/Terminal/Mouse」瘦成纯 host-neutral 编排:

```
visor_pump(host):
  drain poll_event → dispatch_event(ev)     # 宿主消化输入
  render_frame(frame)                        # 宿主渲染+报脏区+给 staging ptr
  for rect in frame.dirty: host->flush(rect, frame.pixels, ...)
```

- **Host ABI 加 2 回调 + 1 POD**([visor_host.h]):`dispatch_event(ctx,ev,payload)`、
  `render_frame(ctx,visor_frame*)` + `visor_rect`(半开脏区)+ `visor_frame`(rect 数组 +
  count + staging pixels/stride/w/h/fmt)。core 拥有 flush 循环;host 拥有 input dispatch +
  rendering + 脏区 policy。
- **adapter 接管全部 cinux 逻辑**([visor_host_cinux.cpp]):`cinux_dispatch_event`(visor_event
  反序列化→`handle_mouse/handle_key`)+ `cinux_render_frame`(图标 spawn / terminal 轮询 /
  光标 footprint / dirty 空则 idle 跳过 / 否则 `composite` + 填脏区 rect + staging ptr +
  clear dirty)。§4c 的脏区 policy(光标/idle/结构全屏)+ §3b 的图标动作 全从 pump 挪进 adapter。
- **visor_event 往返保留**(pump 仍 drain `poll_event` + 交 `dispatch_event`)——host-neutral
  事件模型,给 core 重写留骨架。

结果:`visor_pump.cpp` + ABI 头 + region + swraseter **零 cinux 依赖、可独立编译** → 下步
物理挪进 visor submodule 就是纯机械(`git mv` + `add_library` 重新编译,对齐 Cinux-Base)。

## 顺带:ABI 头风格统一(用户指出)

- **全 visor 头 `#ifndef` guard → `#pragma once`**(对齐仓库 119 个头的约定)+ `errno.hpp`。
  visor 头之前 8 个用老式 `#ifndef`,是我引入的 inconsistency。
- **`typedef struct/enum` → 命名类型**(`struct X{}` / `enum X{}`)。

> 关键:`using X = struct{}` 会创建**匿名 struct 别名**,跨 TU 链接失败(类型在每个 TU 里
> 「unnamed」,函数签名对不上,`-fpermissive` 报错)。故结构体**必须命名**。而**命名形式
> `struct X{}` / `enum X{}` 在 C 与 C++ 都合法**——所以 visor `.h` **仍是 C 兼容**(MCU 纯 C
> host 可 include),既干掉 `typedef` 又不丢 C ABI。`using` 只对「不在函数签名里的类型」安全,
> 但为一致全用命名。

## 测试

- `test_visor_dirty` 重构:3 WM 脏区 unit(invalidate/invalidate_all/clipping)+ 2 pump
  flush-loop(fake `render_frame` 填 rect → pump flush;count==0 idle 不 flush)。脏区 policy
  移 adapter,由 `make run` 冒烟覆盖。
- run-kernel-test 930→**928/0**(pump 测 4→2:policy 移 adapter,非删覆盖)。冒烟桌面无 panic。
  全量编译 + `test_host` 绿(改了公共 ABI 头)。

## 边界 / 下一步

- **pump 现可独立编译** → Step 2 = 物理 submodule 化(`git mv` core 文件到 visor 仓库 +
  CinuxOS CMake `add_library(visor_core STATIC ...)` 重新编译 + profile 注入;visor-02 §6.2)。
  机械、低风险(core 已编译独立)。
- adapter(`visor_host_cinux.*`)留 Cinux 仓库(host 侧,本就该留)。core 挪:ABI 头 + region +
  swraseter + pump + abi_check。
- WM 仍持 `visor::Region`(脏区,§4c)——Cinux 对 visor 的唯一「深」依赖是一个稳定代数 utility,
  可接受(用户重写时可再把 dirty policy 移进 core 合成器)。
