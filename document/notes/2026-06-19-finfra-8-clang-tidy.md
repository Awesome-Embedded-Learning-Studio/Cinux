# F-INFRA I-8 — clang-tidy 精选配置（advisory，本地）

> 日期 2026-06-19 · F-INFRA Tier2 批 I-8 · 分支 `feat/finfra`

## 背景
R8：零静态分析集成。443 处 reinterpret_cast、754 处 new/delete、侵入式链表悬垂风险，全靠人眼。这是 NotNull（I-7）挡不了的——只能静态分析/运行时检测。

## 目标
提供精选 clang-tidy 配置，本地可跑，抓 null 解引用/窄化/cast/slicing 等真问题。

## 设计/决策（验证器 R8 告诫）
- **精选 allowlist**（`.clang-tidy`）：`clang-analyzer-core.*`（null deref/栈逃逸，**最高价值**）、`NewDelete`、`bugprone-{narrowing,suspicious-enum,sizeof,integer-division,unused-raii}`、`cppcoreguidelines-{pro-type-cstyle-cast,slicing}`、`modernize-{use-nullptr,use-override,use-using,loop-convert}`、`readability-{function-size,redundant-*,simplify-boolean-expr}`。
- **禁用** hosted/RTTI/exception/library 假设的检查：`hicpp-*/google-*/cert-err61-cpp/bugprone-exception-escape/cppcoreguidelines-{avoid-c-arrays,no-malloc,pro-bounds-*,owning-memory,init-variables}`；`modernize-deprecated-headers`（内核按约定用 C 头 `<stddef.h>`）；`alpha.webkit.NoUncountedMemberChecker`（无 WebKit 引用计数类型）。
- **`HeaderFilterRegex: ^kernel/(arch|...|gui)/`**：排除 Cinux-Base 子模块（已 -Wpedantic -Werror）+ build/ 生成物。
- **`WarningsAsErrors: ''`**：纯 advisory，不升级为错误。
- **不加 CI 门禁**：clang-tidy 版本偏移（本机 22 vs CI runner）曾致 clang-format CI 被禁（[[format-ci-disabled-version-mismatch]]）。advisory 跑、本地用；阻塞门禁待 LLVM apt 版本固定后再上。

## 验证（实测能跑 + 抓真问题）
- `clang-tidy --version` → 22.1.6；`build/compile_commands.json` 在（CMake 导出）。
- `clang-tidy -p build --config-file .clang-tidy kernel/lib/kprintf.cpp` → 跑通，3 warnings 全在非用户代码被 HeaderFilter 抑制（配置生效）。
- **抓到真问题**：`scheduler.cpp:152` `clang-analyzer-core.NullDereference`——"Access to field 'addr_space' results in a dereference of a null pointer (loaded from variable 'prev')"。这正是 NotNull 挡不了、静态分析才能抓的（印证 I-7 诚实声明：NotNull 只挡 null 契约，deref 分析靠 clang-analyzer）。另有 `modernize-loop-convert` 建议。
- 本地跑：`run-clang-tidy -p build -j$(nproc)`。

## 用法 / follow-up
- 本地：`run-clang-tidy -p build -j$(nproc)`（或单文件 `clang-tidy -p build --config-file .clang-tidy <file>`）。
- findings 消化：`scheduler.cpp:152` 的 prev null deref 值得人工复核（可能是上下文保证非空，也可能是真 bug）——属 triage follow-up，不在本批。
- 阻塞 CI 门禁：待固定 LLVM apt 版本后加（版本对齐再 Werror）。

## 文件
- 新：`.clang-tidy`（根目录）。无内核代码改动，run-kernel-test 不受影响。
