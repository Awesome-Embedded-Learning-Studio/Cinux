# F-INFRA 基建加固里程碑 — 收官总结

> 日期 2026-06-19 · 横切里程碑（插 F4 SMP 前，FO 的延续）· 分支 `feat/finfra`（12 commit，待 PR）

## 起因
F2（内存）/F3（进程线程调度）大方向落地后，用户担心基建不足、代码风格/指针语义可优化、调试/静态检查基建是否值得大力投入。跑了一个 26-agent workflow（6 维代码审计 + 5 维联网调研 + 综合 + 对抗验证 + 完整性审查）得出验证结论，立为正式里程碑 F-INFRA 全量执行。

## 范围（10 批，全 ✅）
| 批 | Tier | 内容 | commit |
|----|------|------|--------|
| I-1 | 0 | CI timeout 包裹 run-kernel-test + 失败上传串口日志（G1/G8） | 0bd947b |
| I-2 | 0 | freestanding 头门禁脚本 + 修 icon_data `<array>` + CMake GCC 版本断言（G9/G2） | b463d3e |
| I-3 | 0 | 警告标志收紧（-Wshadow/-Wold-style-cast/-Wnon-virtual-dtor/-Woverloaded-virtual/-Wformat=2 + -Werror=return-type）+ 清理（17 cast + 15 预存警告 + build-id/RWX 链接）→ 零警告构建（R2） | 3e180ed |
| I-3b | 0 | kprintf/kvprintf/kpanic 加 `format(printf)` 属性 + 修 21 处真实格式不匹配（R2b） | cd20dfe |
| I-4 | 0 | static_assert 锁 5 结构体布局（SlabHeader/SlabCache/LogEntry/VMA + 两处 InterruptFrame offsetof 矩阵）（R11） | 3b9bba2 |
| I-5 | 1 | KALLSYMS 真符号注入（nm POST_BUILD 生成表 + boot 注册，panic backtrace 不再裸地址）（R4） | 1a0b037 |
| I-6 | 1 | 64 位 .gdbinit 重写 + decode-trace.sh addr2line demangle + 修 run-gdb 陈旧路径（R9/G5） | 9eb06b0 |
| I-7 | 2 | NotNull<T> 指针契约类型（kernel/lib）+ scheduler 5 永不为 null 入参采纳；抓出 set_current 为 nullable（R5） | f7f8a10 |
| I-8 | 2 | .clang-tidy 精选 allowlist（advisory 本地，不加 CI 门禁——版本偏移教训）；实测抓 scheduler prev null-deref（R8） | b7ca688 |
| I-10 | 3 | lockdep-Part1：held_spinlock_depth + schedule() 断言（CINUX_LOCKDEP opt-in）（R6） | 6ae6996 |
| I-9 | 3 | UBSAN freestanding 桩（CINUX_UBSAN opt-in，GCC void* builtin 签名，桩调 kpanic）（R1） | 4373232 |

## 关键结果
- **全程基线 840/0 绿、零警告**（默认构建）。改公共接口后补全量 `cmake --build build`（含 host 单测，CI 盲区覆盖）也零警告。
- **UBSAN 构建（opt-in）840/0、零 UB 命中**——F2/F3 全部工作（内存/调度/信号/clone/futex）在默认 UBSAN 检查下 **UB-clean**。强结果。
- **每批主动冒烟验证**：KALLSYMS lookup 解析真名（ok=1）、UBSAN 桩 fire+backtrace、lockdep 双构建无误报。不靠"编译过"凑数。
- **对抗验证阶段纠正了多处提案错误**（采纳时落实）：GCC UBSAN 用 void* 非 uintptr_t（不抄 SerenityOS）、set_current 合法 nullable、`-Werror=implicit-int` 是 C-only 噪声、gdbinit 不能写 0x1000000 偏移、static_assert 实测 32 非 172、check_toolchain 非空壳等。

## 指针语义（用户核心关切）的诚实答案
C++17 freestanding 给不了真 borrow checker。落地的是**分层近似**，每层抓不同 bug 类：
- **NotNull<T>**（I-7）：边界 nullptr 契约（assert→kpanic）。零开销。已采纳 scheduler 入参。
- **clang-analyzer**（I-8）：null-deref / 栈地址逃逸的静态分析（抓到 scheduler prev 假阳性供 triage）。
- **UBSAN**（I-9）：null member call / 溢出 / shift 等运行时 UB。
- **mini-KASAN 红区**：R10，留 follow-up（堆 UAF/溢出）。
vma.hpp 的 Owner/Borrow 词汇早已存在于注释，现在 NotNull 让"永不为 null"机器可见。

## opt-in 工具（默认 OFF，零成本，需要时开）
- `cmake -DCINUX_LOCKDEP=ON`：持锁跨 schedule 检测（F4 SMP 死锁前瞻）。
- `cmake -DCINUX_UBSAN=ON`：UBSan 运行时 UB 检测（F4 前最高价值）。
- `run-clang-tidy -p build`：本地静态分析（advisory）。
- `scripts/decode-trace.sh 0xADDR`：panic 裸地址→demangled 源码位置（一键）。

## 划归 F4-M5（不在本里程碑）
R3 原子引用计数（SharedCwd/SharedSigActions，单核 race 不 live）、R6-Part2 锁序图 DFS（单核 ABBA 不可能）——随 F4-M5 同步原语重构一起做风险更低。

## follow-up（渐进，不拆批）
R7（BUG_ON/WARN_ON + CODING-TASTE assert-vs-Error 判据）、R10 mini-KASAN 红区、R12 next_tid 测试复位、R13 -O0 CI 矩阵、G3 确定性种子、G4 xfail 标记、G7 分层 include 检查、G10 启动阶段计时、KALLSYMS 符号 demangle、clang-tidy findings triage（scheduler prev 假阳性已定性）。

## 验证清单
- 默认构建 `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` → **840/0**。
- 全量 `cmake --build build -j$(nproc)`（含 host 单测）→ **零错误零警告**。
- UBSAN `cmake -B build-ubsan -DCINUX_UBSAN=ON ...` → run-kernel-test **840/0 零 UB**。
- lockdep `cmake -B build-lockdep -DCINUX_LOCKDEP=ON ...` → **840/0 无误报**。
