# F-INFRA I-9 — UBSan freestanding 桩（GCC void* 签名，opt-in）

> 日期 2026-06-19 · F-INFRA Tier3 批 I-9 · 分支 `feat/finfra`

## 背景
R1：零 sanitizer。C++17 -fno-exceptions、全裸指针运算/位操作/除法，signed overflow / shift 越界 / div-by-zero / null member call / 类型不匹配等全是**静默 UB**。F4 SMP 后非确定性下无法 bisect。UBSAN 是 F4 前最高价值检查。

## 目标
`-fsanitize=undefined` + freestanding `__ubsan_handle_*` 桩，UB 命中即 kpanic + backtrace。opt-in（默认 OFF）。

## 设计/决策（验证器 R1 FIX1-5 全部应用）
- **GCC void* 签名（最关键）**：GCC 16 把每个 `__ubsan_handle_*` 当 **builtin 声明、参数全 `void*`**（如 `shift_out_of_bounds(void*,void*,void*)`）。**经验实测确认**——用 uintptr_t（Clang 形式）是硬编译错"definition ambiguates built-in declaration"。**绝不照抄 SerenityOS（那是 Clang）**。桩用 void* 全参匹配。
- **桩**（`kernel/lib/ubsan.cpp`）：21 个非-_abort handler（3 参/2 参/4 参/1 参四组）+ `__ubsan_handle_invalid_builtin`（GCC 16 额外 emit，1 参，迭代发现）。每个经 `ubsan_trap(kind)` → `kpanic("UBSan: %s ...", kind)`。
- **`CINUX_UBSAN` opt-in（默认 OFF）**：`cmake -DCINUX_UBSAN=ON`。编译期 `-fsanitize=undefined -fno-sanitize=alignment`（对齐太噪，Linux CONFIG_UBSAN_ALIGNMENT 默认关）。**只编译期加 -fsanitize，不加链接期**——桩解析符号引用，不拉 libubsan。
- **排除插桩**（防递归 + 守子模块）：Cinux-Base 源（third_party/CMakeLists.txt `-fno-sanitize=undefined`，守其 -Wpedantic -Werror 契约）；诊断/panic 路径（`ubsan.cpp`/`kprintf.cpp`/`backtrace.cpp`/`diagnostics.cpp`/`exception_handlers.cpp`）——UB 命中这些会 UBSAN→kpanic→backtrace→UBSAN 递归。
- **桩调 kpanic 非 panic**：panic 要 InterruptFrame（UBSAN handler 没有），kprintf.hpp 的 `kpanic(fmt,...)` 任意处可调。

## 验证（双构建 + 主动触发冒烟）
- **默认构建（UBSAN OFF）**：ubsan.cpp 静态加入不插桩、未被引用，840/0 干净（零警告、零 ambiguity）。
- **UBSAN 构建（ON，build-ubsan）**：840/0，**零 UB 运行时命中**——F2/F3 全部工作（内存/调度/信号/clone/futex）在默认 UBSAN 检查下 UB-clean。强结果。
- **主动触发冒烟**：临时插运行时 shift 越界（`volatile` 防常量折叠）→ build-ubsan 跑 → **`UBSan: shift out of bounds (undefined behaviour caught at runtime)` + `Backtrace (5 frames)`**——端到端证明桩 fire、panic 路径穿透 UBSAN。还原后双构建复跑 840/0。

## 陷阱
- **`__ubsan_handle_invalid_builtin` 未 stub**：首链 link 报 `buddy.cpp: undefined reference`。探 arity（试 (void*)/(void*,void*)/(void*,void*,void*)，GCC 报歧义揭示 builtin 期望 `(void*)`），补 1 参桩。说明 handler 集**不能照搬别处清单**，须对实际工具链迭代补全。
- **panic 不经 isa-debug-exit**：UBSAN 触发的 kpanic 走 cli;hlt 死循环，QEMU 不干净退出（冒烟 exit=124 是 timeout 杀），属预期（真 panic 本就不返回）。

## 文件
- 新：`kernel/lib/ubsan.cpp`。
- 改：`CMakeLists.txt`（CINUX_UBSAN 选项）、`kernel/CMakeLists.txt`（-fsanitize + 排除诊断路径）、`kernel/lib/CMakeLists.txt`（加 ubsan.cpp 源）、`third_party/CMakeLists.txt`（排除 Cinux-Base）。
