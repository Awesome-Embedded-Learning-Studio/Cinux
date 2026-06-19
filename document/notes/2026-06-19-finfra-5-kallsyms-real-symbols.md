# F-INFRA I-5 — KALLSYMS 真符号注入（panic backtrace 不再裸地址）

> 日期 2026-06-19 · F-INFRA Tier1 批 I-5 · 分支 `feat/finfra`

## 背景
R4 / FO 推迟项「1b 真实符号注入」：KALLSYMS 运行时（`kallsyms_set_table`/`kallsyms_lookup`）早已就绪，但生产零调用——`g_entries` 恒 nullptr，所有 panic backtrace / klog 裸地址，要 host 手动 `addr2line`。FO 因「CMake 两阶段链接风险」推迟。

## 目标
让 panic backtrace 在内核内（无需 host）直接显示函数名。

## 设计/决策（POST_BUILD 单趟法，规避两阶段链接的地址漂移）
- **关键洞察**：`linker.ld` 把 `.rodata` 放在 `.text` 输出段内、且在 `*(.text .*.text)` 之后。kallsyms 表是 const 数据→落 .rodata，**在所有函数之后**。故加入表不会移动任何 .text 函数地址——**对无表（或前次）ELF 单次 nm 即得正确最终地址**，无需 Linux 式多趟收敛。
- **生成器** [generate_kallsyms.py](../../scripts/generate_kallsyms.py)：`nm -n <elf>` 取 `T`/`t`（text 函数），升序输出 `extern "C" const KallsymEntry g_kallsyms_table[]`。**仅在内容变化时写文件**（保 mtime）——否则 POST_BUILD 每次刷新 mtime 会触发无限重建循环；收敛后内容稳定、不再触发重建。
- **CMake**（`kernel/CMakeLists.txt`）：per-target 生成 `kallsyms_data_{test,kernel}.cpp`；configure 期 `[==[...]==]` bracket 原始字符串 seed 空 stub（`if(NOT EXISTS)` 守卫，不覆盖已生成的真表）；POST_BUILD 命令对每个 target 的 ELF 重生成。
- **boot 注册**：`kernel/main.cpp` + `test/main_test.cpp` 在 `kprintf_init()` 后调 `kallsyms_set_table(g_kallsyms_table, g_kallsyms_count)`。test_kallsyms 各 case 先 `set_table` 再断言，不依赖 boot 前置状态，故 test binary 注册真表不干扰测试。
- **CI 构建两次**：首构建编 stub + POST_BUILD 生成真表，次构建编入真表。kernel-tests job Build 步改两次 `cmake --build`。

## 陷阱
- **CMake list 分隔符**：初版用多个 quoted 串 `set(STUB "a\n" "b\n")` 被 CMake 当 list（`;` 分隔），stub 文件每行前多 `;`→`stray '#'` 编译错。改用 bracket `[==[...]==]` 原始字符串解决。
- **mtime 抖动致无限重建**：生成器必须内容比对、变了才写，否则每次 POST_BUILD 刷新 mtime→重编→重链→再生成，死循环。
- **clean checkout 首构建是 stub**：需第二次构建才编入真表（CI 已处理；本地 clean 后跑两遍）。
- **符号名 mangled**（`_ZN5cinux3lib...`）：可读性差但远胜裸地址；内核内 demangle 复杂，留 follow-up（host `c++filt` 可后处理）。

## 验证
- 生成器独立测试：对 big_kernel_test ELF 产 7045 符号，含 `_start`@0xffffffff81000000、`kernel_main` 等。
- 非破坏冒烟：boot 后 `kallsyms_lookup(0xffffffff810083bc)` → `ok=1 '_ZN15test_task_state22test_state_transitionsEv'`（真实函数名），证明端到端解析。
- `timeout 40 ... run-kernel-test` → **840/0**（test_kallsyms/backtrace 无回归）。

## 文件
- 新：`scripts/generate_kallsyms.py`。
- 改：`kernel/CMakeLists.txt`（per-target kallsyms 生成 + POST_BUILD）、`kernel/lib/kallsyms.hpp`（extern 声明）、`kernel/main.cpp` + `kernel/test/main_test.cpp`（boot 注册）、`.github/workflows/ci.yml`（构建两次）。
- 生成物 `build/kernel/kallsyms_data_{test,kernel}.cpp` 不入 git（构建产物）。
