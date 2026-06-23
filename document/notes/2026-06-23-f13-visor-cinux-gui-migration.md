# 2026-06-23 · F13 visor · Cinux-GUI 独立仓库迁移收官(Phase A→C)

> 批:Cinux-GUI 仓库抽取 + submodule 接管(feat/f13-visor,5 commit 待 push)。
> 前置:§4 全域 + pump 解耦 + visor 物理分离到顶层(已合 PR 流程)。

## 动机

用户要把 PC GUI 框架抽成**独立仓库**,便于在 SDL/X11/Wayland host 上并发开发;
**`visor` 名留给用户的嵌入式线**,PC 框架换名。仓库定 `Awesome-Embedded-Learning-Studio/Cinux-GUI`(org,私有)。
管理方式:**git submodule**(用户决策「做成 submodule 吧,好管理」)。

## 四相执行

### Phase A — visor→cinux::gui / cgui_ 改名(`0baa6d0` + `3397e6c` docs)
`visor_*`→`cgui_*` 文件/符号,`visor::`→`cinux::gui::` 命名空间,`cinux::gui::visor_pump`→`cinux::gui::pump`。
内核无 Rect/Region/pump/Surface 撞名(audit clear)。930→928/0(pump 测 policy 移 adapter)。

### Phase B — lift 到 Cinux-GUI 仓库(`a77780e`/`9a17833` 物理分离 + lift)
`git subtree split` 抽 `cgui/` 历史成独立分支 `cinux-gui-seed`,加 seed README + .gitignore,
独立仓库根构建 + ctest 绿。用户 push 到 org Cinux-GUI:main。
**GOTCHA(身份)**:seed README commit 我 `-c user.email` 覆盖成编的 `charliechen@users.noreply.github.com`
—— 那是**另一个 CharlieChen 账号**的 noreply,GitHub 按邮箱认人,commit 被认成陌生人。
修:`--amend --reset-author` 回 git config 真身(`Charliechen114514 <725610365@qq.com>`)+ force-push。
教训记 [[dont-fabricate-git-author]]。

### Phase B2 — 去 cgui_ 前缀 + C ABI 进命名空间(submodule `c15bd9d` + `7d3b9aa`)
用户:「都命名空间了,别加 cgui 前缀,仓库叫 Cinux-GUI 你加这干啥」。选**全去 + C ABI 进 `cinux::gui`**:
- 文件去前缀 + `.h`→`.hpp`(`cgui_host.h`→`host.hpp` 等),顺手修 `swraseter`→`swraster` 拼写。
- C ABI 进 `cinux::gui`(`Host`/`Frame`/`HostCore`/`HostDesktop`/`EventHeader`/`PointerPayload`/`KeycodePayload`),
  `PixelFormat` 改 enum class,事件常量改 k-前缀 constexpr,`cgui_rect` 并入已有 `Rect`,**去 extern "C"**(MCU 砍,无 C 消费者)。
- **MCU 砍**:删 `conf.hpp`(profile/buffer-mode/NO_FPU 全 MCU 机制),`HostCore` 去 enter/exit_sleep + next_deadline_ms,PixFmt 只留 XRGB/ARGB。
- CMake target `cgui_core`→`cinux-gui`,harness exe `cgui_host_smoke`→`cinux-gui-smoke`。
- **GOTCHA(撞名)**:核心事件枚举原名 `EventType`,但内核 `kernel/gui/event.hpp` 已有 `cinux::gui::EventType`(Mouse*)——
  同命名空间两个 `enum class EventType` **重定义**。改核心的为 **`EventCode`**(内核无,不撞;内核 EventType 不动)。
  `EventHeader.type` 字段跟随。这是选 `cinux::gui` 扁平命名空间时埋的雷,EventType 是唯一撞点。
- 独立构建(hosted 编译器零 kernel include)零警告 `-Wall -Wextra` + ctest 1/1。
- harness `fake_host_main.cpp`:`g_stage` 全局 buffer(只取地址、内容没用)按「别这样声明」挪成 `fake_render_frame` 内 `static` 局部。

### Phase C — submodule 接管(`2d65eba`)
in-tree `cgui/` 删,`third_party/Cinux-GUI` submodule(pin `7d3b9aa`)接管。消费者迁移:
- adapter `cgui_host_cinux`→`host_cinux`;类型 `cgui_*`→`cinux::gui::*`;`EventCode::kPointer/kKeycode`
  (内核 `EventType::Mouse*/Key*` 不动);删已去 MCU 字段赋值。
- window_manager / gui_init / init.cpp / 3 test:include 改 `third_party/Cinux-GUI/core/*`。
- CMake:`cgui_core`→`cinux-gui`(link),adapter 文件名,test `test_cgui_*`→`test_gui_*`(+swraseter→swraster,`run_cgui_*`→`run_gui_*`)。
- 注释/日志 `cgui`→`cinux::gui`。

## 验证(全绿)
| | 结果 |
|---|---|
| Cinux-GUI 独立构建(hosted 编译器,零 kernel include) | `libcinux-gui.a` + `cinux-gui-smoke`,零警告 `-Wall -Wextra -Werror` |
| 独立 harness + ctest | 1/1(null-host 安全 + idle-skip + dirty-flush + region 代数) |
| CinuxOS 全量 build | big_kernel + big_kernel_test 链入 cinux-gui,零错 |
| **run-kernel-test**(QEMU,timeout 40) | **928/0** |
| GUI 冒烟(`--target run`) | Framebuffer 1024x768 → GUI → host adapter 初始化 → desktop 渲染。**零 panic** |
| test_host(CI 对等盲区) | 全绿 |

## 关键设计(防回退)
- **核心是 host-neutral DAG,adapter 是唯一叶子消费者**——`third_party/Cinux-GUI/` 子树可独立开发/构建/lift。
- **submodule pin 必须先于父仓 push**:父仓 pin submodule SHA,submodule 远程得先有该 SHA,否则 dangling(CI/他人 clone 失败)。顺序:submodule push → 父仓 push。
- **`cinux::gui` 扁平命名空间的代价**:与内核 `cinux::gui::EventType` 撞 → 核心用 `EventCode`。未来核心新增类型仍需查内核 cinux::gui 是否同名。
- **sed 顺序**:include 规则必须在 symbol 规则前(否则 `cgui_host.h` 被 `cgui_host`→`Host` 啃成 `Host.h`)。zsh 不 word-split 未加引号变量 → 用 xargs。

## 终态 / lift 边界
```
third_party/Cinux-GUI/   # submodule,独立仓库
├── core/                # cinux::gui 命名空间,PC-only,stdint/stddef only
│   ├── host.hpp         # ← 唯一硬边界(Host ABI 表)
│   ├── event.hpp / event_payload.hpp   # EventCode/EventHeader/payloads
│   ├── pump.hpp/.cpp    # 表驱动 pump(零 host include)
│   ├── region.hpp/.cpp  # Rect + bounded Region
│   ├── swraster.hpp/.cpp# 纯 CPU 整数绘制原语
│   └── abi_check.cpp    # 编译期 ABI 自检
└── host/fake_host_main.cpp  # 中立性证明 + SDL/X11/Wayland adapter 种子
```
CinuxOS 侧:仅 `kernel/gui/host_cinux.{hpp,cpp}`(Cinux host adapter)+ WindowManager/Terminal 等 GUI 应用代码。

## 后续
- Cinux-GUI 主体(widgets/合成器 M0-M9)用户在 SDL 项目独立开发(换 `host/` 表填)。
- Cinux 侧 visor 收窄为 host adapter + GUI 应用。
- F13 follow-up:dirty lowering / 合成器只重绘脏区 / SMP TLB shootdown。
