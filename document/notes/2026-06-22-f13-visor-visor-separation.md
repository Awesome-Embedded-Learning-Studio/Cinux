# 2026-06-22 · F13 visor · Step 2 物理分离到顶层 visor/ + 独立构建

> 批:`visor 物理分离`(feat/f13-visor,未 push)。
> 前置:Step 1 pump 解耦(`8a4add4`,visor_pump 零 cinux include)。

## 动机

用户要**加速分离**以便并发开发:「我的想法就是尽快的去跑通 ABI 中立性……我也马上
在另一个项目开始用 SDL 模拟 + 实操在 F1~F4 几个芯片」。Step 1 已把 pump 解耦成
host-neutral(构造上证明零 cinux 耦合),但文件仍混在 `kernel/gui/visor_core/`。
Step 2 把 host-neutral 核心物理搬到顶层 `visor/`,给出干净归属边界 + **可独立构建的
中立性证明**(visor 当独立 GUI 库 / SDL/MCU 种子)。

用户选「全量搬到顶层 visor/ + 独立构建」(4 选项中最重、归属最清晰)。

## 做了什么

### 1. 文件搬迁(`git mv`,git 保留 rename 历史)

| 类别 | 源 | 目标 |
|---|---|---|
| host-neutral 核心(11 文件) | `kernel/gui/visor_core/` | **`visor/core/`** |
| Cinux host adapter(2 文件) | `kernel/gui/visor_core/` | **`kernel/gui/`**(留 GUI 子系统) |

核心:host.h / event.h / event_payload.h / conf.h / pump.hpp+cpp / region.hpp+cpp /
swraseter.hpp+cpp / abi_check.cpp。adapter:visor_host_cinux.hpp+cpp。`kernel/gui/visor_core/`
目录清空删除。

### 2. include 路径(三类,全机械)

- **核心内部互引**:全是同目录 bare include(`visor_host.h` 里 `#include "visor_event.h"` 等)。
  搬到 `visor/core/` 后仍同目录 → **零改动**。
- **adapter**(搬到 `kernel/gui/`,不再是核心同目录):bare include 加前缀
  → `#include "visor/core/visor_event.h"` 等(仓库根已在 include 路径上)。
- **外部消费者**(window_manager.hpp / init.cpp / gui_init.cpp / 3 个 test_visor_*.cpp):
  原用 `kernel/gui/visor_core/visor_X` 仓库根相对路径 → 改 `visor/core/visor_X`
  (adapter 引用改 `kernel/gui/visor_host_cinux.hpp`)。

顺带把所有搬动文件的 `@file` doxygen 路径注释同步更新。

### 3. CMake 接线

- **`visor/CMakeLists.txt`**(新,单文件双用):
  - `add_library(visor_core STATIC …)`——4 个核心 .cpp(pump/region/swraseter/abi_check),
    `target_include_directories(PUBLIC core/)`,`cxx_std_17`。
  - **双构建守卫** `if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)`:
    作为 kernel 子目录(cinux 项目)只建 lib;作为 build 根(`cmake -S visor`)额外
    声明 `project()` + 建 `visor_host_smoke` harness + `add_test` + `-Wall -Wextra`
    (对齐 kernel 零警告纪律)。
- **根 `CMakeLists.txt`**:`if(CINUX_GUI) add_subdirectory(visor) endif()` 放在
  `add_subdirectory(kernel)` **之前**(visor_core 目标要先存在,kernel 才能链)。
- **`kernel/CMakeLists.txt`**:`big_kernel` + `big_kernel_test` 各加
  `if(CINUX_GUI) target_link_libraries(… PRIVATE visor_core) endif()`。
- **`kernel/gui/CMakeLists.txt`**:删 4 个核心 .cpp 的 `target_sources`(归 visor_core lib),
  只留 `visor_host_cinux.cpp`(adapter)。

### 4. 独立构建中立性证明 —— `visor/host/fake_host_main.cpp`

一个**零 kernel** 的 host 程序:手填 `visor_host` 表(fake_poll_event/dispatch_event/
render_frame/flush),驱动 `cinux::gui::visor_pump()`。断言:null-host 安全 no-op /
idle 帧 count==0 不 flush / dirty 帧一 rect→flush(x=1,y=2,w=2,h=2) 一次 / region 代数
(intersect + Region.add)。它是 SDL/MCU host adapter 的**种子**(换表填即换 host)。

## 验证(全绿)

| 验证 | 结果 |
|---|---|
| **独立构建**(hosted `/usr/sbin/c++`,零 kernel include) | `libvisor_core.a` + `visor_host_smoke` 编译通过,`-Wall -Wextra -Werror` 零警告 |
| 独立 harness + ctest | **1/1 passed**(null-host 安全 + idle skip + dirty flush + region 代数) |
| 全量 `cmake --build build` | big_kernel + big_kernel_test 链入 visor_core,零链接错 |
| **run-kernel-test**(QEMU 真内核,timeout 40) | **928 passed, 0 failed** |
| GUI 冒烟(`--target run`,timeout 40) | Framebuffer 1024x768 → GUI 子系统 → **visor Cinux host ABI adapter 初始化✓** → gui_worker → desktop icons(Shell/Calculator)→ composited。**零 panic** |
| `test_host`(CI 对等盲区) | 全绿,含 window / window_manager / terminal |

## 关键设计(防回退)

- **核心是 host-neutral DAG,adapter 是唯一叶子消费者**:核心从不依赖 adapter,
  只反向。这就是「能原样拷走」的构造保证——`visor/core/` + `visor/CMakeLists.txt`
  + `visor/host/` 整棵子树可 verbatim 拷进 SDL/MCU 项目。
- **双构建单 CMake 文件**:`CMAKE_SOURCE_DIR == CMAKE_CURRENT_SOURCE_DIR` 守卫让
  同一 `visor/CMakeLists.txt` 既是 kernel 子目录(继承 cinux 工具链)又是独立项目
  (hosted 编译器 + harness)。中立性证明 = 同一份核心 TU 在两种工具链下都编译+跑通。
- **仓库根已在 include 路径**(`big_kernel_common` PUBLIC 给了 `${CMAKE_SOURCE_DIR}`):
  消费者用 `visor/core/visor_X` 路径直接解析,**CMake include-dir 零改动**。

## lift 清单(用户拷去 SDL/MCU 项目)

```
visor/
├── CMakeLists.txt      # 双构建:子目录 lib / 独立 lib+harness
├── core/               # host-neutral 核心,stdint/stddef only
│   ├── visor_host.h    # ← 唯一硬边界(Host ABI 表)
│   ├── visor_event.h / visor_event_payload.h / visor_conf.h
│   ├── visor_pump.hpp / .cpp      # 表驱动 pump(零 host include)
│   ├── visor_region.hpp / .cpp    # Rect + bounded Region
│   ├── visor_swraseter.hpp / .cpp # 纯 CPU 整数绘制原语
│   └── visor_abi_check.cpp        # 编译期 ABI 自检
└── host/
    └── fake_host_main.cpp  # 中立性证明 + SDL/MCU adapter 种子
```

拷走后:换 `host/` 下的表填(SDL 的 poll/render/flush 或 MCU 的 SPI-DMA flush),
核心 M0-M9 widget/合成器在另一项目独立开发。

## 后续

- visor 主体(M0-M9 widget / 合成器)用户在 SDL 项目独立开发。
- Cinux 侧 visor 收窄为 L7 host adapter(`kernel/gui/visor_host_cinux.cpp`)。
- dirty lowering / 合成器脏区优化见 visor-02 §4.4 / §5(follow-up)。
