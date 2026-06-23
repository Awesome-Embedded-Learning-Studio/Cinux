# 2026-06-22 · F13 · visor→cinux::gui / cgui_ 改名(迁 Cinux-GUI 独立仓库前)

> 批:`visor 改名切割`(feat/f13-visor,commit `0baa6d0`,未 push)。
> 动因:用户决定 PC GUI 框架迁去独立仓库 `Cinux-GUI`,**visor 名留给嵌入式线**,
> 故全树改名切割身份。namespace 选 `cinux::gui`(与内核 WindowManager/Window/
> Terminal 同命名空间并存,审计确认零符号撞车)。

## 改名映射

| 维度 | 旧(visor) | 新 |
|---|---|---|
| C++ 命名空间 | `visor::` | `cinux::gui::` |
| 主入口函数 | `cinux::gui::visor_pump` | `cinux::gui::pump`(命名空间带身份,去前缀) |
| C ABI 全局前缀 | `visor_` / `VISOR_` | `cgui_` / `CGUI_`(C 结构体进不了命名空间) |
| 文件/目录 | `visor/core/visor_*` | `cgui/core/cgui_*`、`visor/`→`cgui/` |
| adapter | `kernel/gui/visor_host_cinux.*` | `kernel/gui/cgui_host_cinux.*` |
| adapter 函数 | `cinux_visor_host(_init)` | `cinux_host(_init)` |
| 测试 | `test_visor_*` | `test_cgui_*` |

## 机械 6 步(有序 sed + perl)

1. `s/visor::/cinux::gui::/g` —— C++ 命名空间**用法**
2. `s/VISOR_/CGUI_/g` —— 宏
3. `s/visor_/cgui_/g` —— 所有 `visor_` 前缀(C ABI 结构体、文件名、目标名、测试函数)
4. `perl s/cgui_pump(?!\.[hc]p{0,2})/pump/g` —— pump**函数** `cgui_pump`→`pump`,
   负向前瞻保护文件名 `cgui_pump.{hpp,cpp,h}` 不被改(函数与文件名分离)
5. `s/\bvisor\b/cgui/g` —— 残留整词 visor(prose「visor core」+ 目录路径 `visor/core/`)
6. `s/cinux_cgui_host/cinux_host/g` —— adapter 函数去冗余 cgui

## GOTCHA:命名空间声明被 step5 误中

step1 只处理 `visor::`(**用法**),不处理 `namespace visor {`(**声明**,visor 后跟空格/`{`
非 `::`)。step5 `\bvisor\b` 把声明 `namespace visor {` 误改成 `namespace cgui {`,
但用法已是 `cinux::gui::Rect` → **声明在 `cgui::`、用法在 `cinux::gui::`,编译报
`'cgui::Rect' declared here` 但找不到 `cinux::gui::Rect`**。

修:`namespace cgui {`→`namespace cinux::gui {`(4 文件:region.hpp/cpp +
swraseter.hpp/cpp,各含 1 声明 + 1 闭合注释)。cgui_pump.hpp/cpp 原本就是
`namespace cinux::gui`(pump 一直在该命名空间),未受影响。

## 漏文件教训

初次 sed 清单只列了 window_manager.**hpp**(漏 .cpp),而 window_manager.cpp 有
`visor::Rect` **代码**(非注释),全树扫 `\bvisor\b` + `visor::` 才抓到。还漏
canvas.hpp / gui_init.hpp / data/icon_data.hpp(纯注释)。教训:改名后**全树 grep
整词 + 命名空间用法**双扫,不能只靠初始清单。

## 审计:cinux::gui 与内核零撞车

框架类型(Rect/Region/pump/Surface/ClipRect/fill_rect/glyph_blit)放 `cinux::gui`,
内核现有 WindowManager/Window/Terminal/Event 同命名空间并存。grep 确认:
- `Region` 命中全是注释(「Physical Region Descriptor」「Memory Region」),非类型
- `pump` 命中全是注释(「the pump flushes」),函数 `pump` 原本不存在
- `fill_rect` 命中是 `cinux::drivers::Framebuffer::fill_rect`(成员,不同命名空间)

C++ 允许多个 TU 向同一命名空间追加声明,只要符号名不重复 → 合并合法。

## 验证(全绿)

- 独立 cgui 构建(hosted 编译器,零 kernel include)+ harness ctest 绿
  (`cgui_host_smoke: OK`)
- 全量 `cmake --build build`(cgui_core 链入,test_cgui_* 编译)
- **run-kernel-test 928 passed / 0 failed**(ALL TESTS PASSED)
- GUI 冒烟 `[cgui] Cinux host ABI adapter initialised` + desktop 渲染,**零 panic**
- `test_host` 49/49(window / window_manager / terminal 绿)
- 全树零 `visor` / `VISOR_` 遗留

## 后续

- **Phase B**:lift `cgui/` 到 `Cinux-GUI` 仓库(subtree split + 结构),用户 push。
- **Phase C**:CinuxOS 把 in-tree `cgui/` 换成 `third_party/cgui` submodule 锁 Cinux-GUI。
- MCU-specific ABI 面(`CGUI_PIX_1BPP`/`RGB565`、`enter_sleep`/`exit_sleep`、
  `next_deadline_ms`)PC-only 后可裁剪,follow-up。
