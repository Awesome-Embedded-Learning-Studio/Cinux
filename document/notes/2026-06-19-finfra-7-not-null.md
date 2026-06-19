# F-INFRA I-7 — NotNull<T> 指针契约类型 + scheduler 采纳

> 日期 2026-06-19 · F-INFRA Tier2 批 I-7 · 分支 `feat/finfra`

## 背景
R5：全裸 `T*`（~176 处），所有权/非空靠注释。用户核心关切——Rust Borrow/Observe 风格指针语义。C++17 freestanding 给不了真 borrow checker，但可用 `NotNull<T>` 把"永不为 null"做成机器可见契约。

## 目标
引入 `NotNull<T>` 词汇类型 + 在 scheduler 永不为 null 的入参上采纳，证明可用。

## 设计/决策
- **放置 `kernel/lib/not_null.hpp`（命名空间 `cinux::lib`）**：DIRECTIVES A 说 Cinux-Base 子模块改动属另一仓库（跨库 PR 摩擦大）。但 `kprintf`/`klog` 本就是 `cinux::lib` 定义在 `kernel/lib/`——故 `kernel/lib/` 放 `cinux::lib` 工具类型是**既有惯例**。not_null 日后可晋升 Cinux-Base（host 测试要用时）。
- **设计**（仿 gsl::not_null，freestanding 精简）：单 `ptr_` 成员；隐式 ctor from `T`（`assert(ptr_!=nullptr)`，经 crt_stub→kpanic）；`delete` `nullptr_t` ctor/assign；`operator T()` 隐式转回（使调用方/实现近乎零改动）；`operator*`/`->`/`get()`。零开销。
- **采纳范围**（低风险）：Scheduler **静态方法** `add_task/remove_task/run_first/block/unblock` 入参 `Task*`→`NotNull<Task*>`。**不动虚接口** `SchedulingClass::enqueue/dequeue`（改 vtable 签名 churn 大，波及 RoundRobin+未来类+mock）。
- **诚实**：NotNull 只挡 null，**挡不了 use-after-free/dangling**——那要 Clang 静态分析（I-8）+ mini-KASAN（R10）。它是契约标记，非 borrow checker。

## 陷阱（采纳过程抓 bug）
- **`set_current` 是 nullable**：初版把 `set_current` 也套了 NotNull，但 `test_process_group.cpp` / `test_cwd_stat.cpp` 用 `set_current(nullptr)` **清理/重置** current_——合法置空。NotNull 的 deleted `nullptr_t` ctor 直接编译期拒绝 4 处。**回退 set_current 为 `Task*`**（注释标明 nullable 原因）。这正是 NotNull 的价值：把"接受 null 清理"的 API 和"永不为 null"的 API 在类型层区分开。其余 5 个方法测试不传 null，保持 NotNull。
- **双向隐式转换**使采纳近乎零改动：调用方传 `Task*` 自动构造 NotNull（断言非空），实现体内 `task` 经 `operator T()` 自动转回 `Task*` 传给 `enqueue(task)` 等。

## 验证
- `cmake --build build --target run-kernel-test` → **840/0**（零警告、零错误）。
- set_current 回退后 5 个 NotNull 入参编译通过、测试全绿。

## 文件
- 新：`kernel/lib/not_null.hpp`。
- 改：`kernel/proc/scheduler.hpp`（include + 5 入参 NotNull，set_current 留 nullable）、`kernel/proc/scheduler.cpp`（对应 5 定义签名）。
