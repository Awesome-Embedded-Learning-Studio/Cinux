# F-INFRA I-2 — freestanding 头门禁 + 修 `<array>` + GCC 版本断言

> 日期 2026-06-19 · F-INFRA Tier0 批 I-2 · 分支 `feat/finfra`

## 背景
G9/G2：`kernel/gui/data/icon_data.hpp` 已违规 `#include <array>`（freestanding 禁用 STL 容器，DIRECTIVES A），但全仓库无任何门禁防止 STL 头蔓延；`scripts/check_toolchain.sh` 虽非空壳但 CMake 无编译器版本断言，GCC 大版本升级是潜伏炸弹（尤其 I-3 严警告 / I-9 UBSAN 对版本敏感）。

## 目标
1. 修现存 `<array>` 违规。
2. 加 CI 门禁脚本，未来任何 STL 头进 kernel/ 即红。
3. CMake configure 时断言 GCC 版本下限，老工具链 fail-fast。

## 设计/决策
- **替换 `<array>`**：消费者（[gui_init.cpp](../../kernel/gui/gui_init.cpp)、[desktop_icon.hpp](../../kernel/gui/desktop_icon.hpp)）只用 `.data()`（取 `const uint32_t*`），`build_icon` 内部用 `pixels[i]` 索引。故在 `detail` 命名空间加 freestanding 聚合 `struct IconBitmap { uint32_t pixels[1024]; operator[]; data(); }`（constexpr、零堆、聚合可 `{}` 零初始化），`build_icon` 返回它，两个图标定义改 `inline constexpr auto ... = detail::build_icon(...)`。Cinux-Base 尚无 `Array<T,N>`（DIRECTIVES A 备注），故就地聚合、不碰子模块。
- **`check_freestanding_headers.py`**（仿 [check_line_limits.py](../../scripts/check_line_limits.py)）：黑名单 STL 容器/string/stream/memory/algorithm/`<atomic>`/`<thread>`/exception/RTTI 等；`os.walk` kernel/（排除 mini/、test/，与 line-limits 一致）；违规即 exit 1 + 提示替代品（cinux::lib::Span/StringView/Atomic、slab、本地聚合）。
- **CMake GCC 断言**：`CMAKE_CXX_COMPILER_ID == GNU` 且 `< 11` → `FATAL_ERROR`。下限 11（C++17 freestanding + 严警告 + UBSAN 的安全底；CI GCC13 / 本机 GCC16 均过）。不设上限（避免误伤本机 GCC16）。Clang 非配置工具链，留空。
- **CI**：line-limits job 加一步跑该脚本（纯 python3，无需 QEMU）。

## 陷阱
- **`os.walk` 只遍历目录**：脚本只接受目录路径；传单文件不生效。CI 扫 `kernel/`（目录）正确。实测：kernel/ 零违规、临时目录含 `<vector>/<memory>` 被抓 exit 1。
- **诊断噪音**：连续 Edit 时 IDE 报中间态错误（"No template array"），需重读真实文件状态确认——最终问题仅外层命名空间的 `IconBitmap` 未限定，用 `auto` 解（从 `detail::build_icon` 推导）。

## 验证
- `python3 scripts/check_freestanding_headers.py` → OK（kernel/ 零违规）；临时目录含违禁头 → exit 1。
- `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` → **840/0**（GCC 断言过、icon_data.hpp 重构编译、GUI 回归全绿）。
- icon_data.hpp 211 行（< 500 软上限）。

## 文件
- 新：`scripts/check_freestanding_headers.py`。
- 改：`kernel/gui/data/icon_data.hpp`（去 `<array>` + `IconBitmap` 聚合 + `auto` 图标定义）、`CMakeLists.txt`（GCC 版本断言）、`.github/workflows/ci.yml`（line-limits job 加 freestanding 头检查步）。
