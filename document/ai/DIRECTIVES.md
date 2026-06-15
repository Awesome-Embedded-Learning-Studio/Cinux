# CinuxOS DIRECTIVES — 架构铁律 / 约定 / 操作模型

> Tier 1（年级稳定，改动稀少）。事实来源 `document/design/cinuxbase-design.md` §1（已核对）。`[待补]`=需填实。改本文件前按操作模型 L4 查牵连。

## A. 架构不变量（跨切面，所有代码适用）
- C++17，不用 C++20。禁异常（throw/try/catch）→ 错误经 `cinux::lib::ErrorOr<T>`。禁 RTTI（dynamic_cast/typeid）。
- **Cinux-Base 契约**：header-only、全 `constexpr`、**无堆分配**（禁 new/malloc/::operator new）、命名空间 `cinux::lib`、单 `.hpp` ≤ 400 行、零外部依赖；允许头 `<cstddef> <cstdint> <cstdarg> <type_traits> <utility> <cstring>`，禁 `<vector> <string> <memory> <iostream> <algorithm>`。
- **子模块边界**：`ErrorOr`/`Error`/`StringView`/`Span`/`Buffer` 在 `third_party/Cinux-Base/include/cinux/*.hpp`；**勿在 `kernel/` 重写**。子模块改动属另一仓库。注：`Array<T,N>` 尚未由 Cinux-Base 提供。
- **层化**：`kernel/<subsys>/` 消费 `cinux::lib`；不反向依赖。
- **syscall 翻译边界**：内核内部一律 `ErrorOr`；仅 syscall trap 入口翻成用户态 `int`/errno（`NotFound→ENOENT`、`PermissionDenied→EACCES`、`OutOfMemory→ENOMEM`…）。`ErrorOr` 不泄漏到用户可见 ABI。
- 子系统架构细节见 `document/design/`；里程碑静态规划见 `document/todo/`。

## B. 编码 / 注释约定
详见 `CODING-TASTE.md`（单一权威：命名/注释/ErrorOr 惯用法/clang-format/测试/panic）。要点：
- 命名：类型 `PascalCase`、函数/变量 `snake_case`、私有成员后缀 `_`(必须)、常量与枚举值 `kPascalCase`(目标，legacy UPPER_SNAKE/PascalCase 迁移中)、宏 `UPPER_SNAKE`。
- 注释一律英文（Doxygen 文件/API 头 + `//` 行内）。
- 机械风格以 `.clang-format` 为准（4 空格/K&R/100 列/namespace 不缩进/指针左），跑 clang-format 不手调。
- ErrorOr：成功 `return value;` / `return {};`，失败 `return Error::Xxx;`，调用方 `if(!r.ok()){...}`。`[Error→errno 映射表批4 补]`

## C. 操作模型（长期，Claude 主力开发）
- **L1 一批一commit一验证**：`cmake --build build --target run-kernel-test -j$(nproc)` 全绿才提交；红则不提交、不更新 PLAN。
- **L2 提交信息** `<type>(<scope>): <中文简述>`——纯描述变更（里程碑/批归属由 PLAN.md 的 commit 列跟踪，不入 commit msg），**不带 Co-Authored-By 或任何 AI 署名 trailer**。
- **L3 propose-then-execute**：新里程碑/跨子系统大改，先出草案等确认；已确认的批内可自主推进。
- **L4 改前查牵连**：改任何模块或文档前，grep 引用方与依赖；ROADMAP/PLAN/`document/todo`/git 状态变更需同步，降不一致。
- **L5 验证只用 `run-kernel-test`**；`run`(GUI 无断言)/`test_host`(host mock 不跑真内核)/`make run` 均非验证。
- **L6 省 token**：命令与文档保持紧凑，不堆仪式；`CLAUDE.md` 常驻须薄，重内容按需读。
- **L7 编译并行**：所有 `cmake --build` 都带 `-j$(nproc)`（本机 14 核）；验证即 `cmake --build build --target run-kernel-test -j$(nproc)`，大幅省编译时间。
