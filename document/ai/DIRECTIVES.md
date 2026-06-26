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
- **L5 验证**：内核改动用 `run-kernel-test`（QEMU 真内核 ~662 项，首选）；`run` target(GUI 无断言)非验证。**⚠️ 根目录无 Makefile——本地一律 `cmake --build build --target <name>`**（`run`/`run-smp`/`run-kernel-test`/`test_host` 等；定义见 `cmake/qemu.cmake`+`test/CMakeLists.txt`。`make <target>` 仅 CI 在 build 目录用，根目录 `make run`/`make test_host` 跑不了）。**防挂死（务必）**：两者都启动 QEMU，崩在 panic 死循环 / 死锁会挂死终端不返回——一律用 `timeout 40` 包裹：`timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` 与 `timeout 40 cmake --build build --target run`。增量构建 + 跑完远不到 40s；挂死时 timeout 杀进程、暴露非零退出码便于诊断（编译步本身不挂死，可单独 `cmake --build build --target big_kernel_test -j$(nproc)` 先编后跑）。**CI 对等盲区**：`run-kernel-test` 不编译 `test/unit/` host 单测（host mock，不跑真内核），而 CI 的 host-tests job 另跑 `test_host` target 当构建门禁（在 build 目录 `make test_host`）——改公共接口/`InodeOps`/被 mock 的类后，push 前补 `cmake --build build -j$(nproc)`（全量，含 host 编译）或 `cmake --build build --target test_host`，否则 host 单测破了本地 run-kernel-test 抓不到（批2b 教训，2026-06）。
- **L6 省 token**：命令与文档保持紧凑，不堆仪式；`CLAUDE.md` 常驻须薄，重内容按需读。
- **L7 编译并行**：所有 `cmake --build` 都带 `-j$(nproc)`（本机 14 核）；验证即 `cmake --build build --target run-kernel-test -j$(nproc)`，大幅省编译时间。
- **L8 工作记录**：**每完成一批立即写一篇 `document/notes/<date>-<milestone>-<batch>.md`**（不堆到里程碑末尾合并；完成一批补一批笔记——参考 `2026-06-16-f1-m3-dma-{buffer,pool,prdt-builder}.md` 各一批一篇的范式）。正式发布质量：背景/目标/设计/决策/陷阱/验证，参考 `document/notes/` 既有风格。2026-06-16 起取代旧 DEVLOG 编年日志（`DEVLOG.md` 已归档留壳，不再追加）。
- **L9 质量门禁**：改代码前按 `QUALITY-GATES.md` 做预审（风险等级/风险域/验证矩阵）；提交前按 G0-G8 审查。系统性审计按 `document/todo/quality/audit-guide.md`，每轮产出 `document/todo/quality/reports/` 报告，发现登记到 `document/todo/quality/debt.md`，修复一债一批闭环。
