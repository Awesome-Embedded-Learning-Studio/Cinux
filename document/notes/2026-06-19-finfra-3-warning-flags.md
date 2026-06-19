# F-INFRA I-3 — 警告标志收紧 + 零警告构建

> 日期 2026-06-19 · F-INFRA Tier0 批 I-3 · 分支 `feat/finfra`

## 背景
R2：内核本体只有 `-Wall -Wextra`，而它消费的 Cinux-Base 子模块却用 `-Wpedantic -Werror -Wold-style-cast -Wshadow -Wnon-virtual-dtor`——库比用库的还严。指针转换/shadow/虚析构类 bug 在本体完全不挡。

## 目标
把内核本体的警告纪律提到子模块水平，达成**零警告构建**（暂不上 blanket -Werror，留 CINUX_WERROR toggle follow-up）。

## 设计/决策
- `kernel/CMakeLists.txt` 的 `BIG_KERNEL_COMPILE_OPTIONS` 加：`-Wshadow -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual -Wformat=2 -Werror=return-type`。
- **不上 `-Wconversion`/`-Wpedantic`**：内核用 GNU inline-asm 扩展；`-Wconversion` 太噪（留 follow-up G6 渐进）。
- **`-Werror=return-type`** 是近零噪声的硬错误（漏 return 立即编译失败）。

## 陷阱
- **`-Werror=implicit-int`/`-Werror=implicit-function-declaration` 是 C-only**：对 C++ 内核只产生 "not valid for C++" 噪声（验证阶段建议的三个里这两个误荐），删掉，只留 `-Werror=return-type`。
- **`-Wformat=2` 现在管不到 kprintf**：kprintf 无 `__attribute__((format))`，编译器把 fmt 当不透明串——零检查。这是 I-3b 的事（加属性 + 清理暴露的真实格式不匹配）。

## 清理（达成零警告）
1. **17 处 old-style-cast**（`-Wold-style-cast`）：`(unsigned)x`→`static_cast<unsigned>(x)`、`(void*)p`→`reinterpret_cast<void*>(p)`。散布在 [backtrace.cpp](../../kernel/arch/x86_64/backtrace.cpp)、[diagnostics.cpp](../../kernel/mm/diagnostics.cpp)、[ahci.cpp](../../kernel/drivers/ahci/ahci.cpp)、test_backtrace.cpp、test_kallsyms.cpp。
2. **15 处预存 -Wall/-Wextra 警告**（之前就在，未致构建失败故未清，I-3 顺手清掉达成真零警告）：
   - gui_init.cpp 3 个死计数器（`tick_count/total_events/mouse_events`，累加无消费者）→ 删。
   - test_ahci_write.cpp `Ext2Pair{nullptr,nullptr}` 漏 `blk_dev` → 补第三个字段。
   - 5 处 `-Wvolatile`（`++` on volatile，C++20 弃用）：并发测试计数器 `task_a_count++`/`task_b_count++`/`shared_counter++` + busy-wait `i++` → 显式 `v = v + 1`（保 volatile RMW 语义）。
3. **链接器 build-id 警告**：自定义 linker.ld 丢弃 `.note.gnu.build-id`，GCC 默认 `--build-id` 被忽略 ld 报警 → 加 `-Wl,--build-id=none` 显式禁用（内核固定地址加载，build-id 无意义）。

## 验证
- `cmake --build build --target big_kernel_test` → **real warnings: 0**（不含 cc1plus 噪声）。
- `timeout 40 cmake --build build --target run-kernel-test -j$(nproc)` → **840/0**（回归全绿）。
- 改动文件 `git diff --stat`：11 文件 +52 -37。

## 文件
- 改：`kernel/CMakeLists.txt`（标志 + 链接 build-id）、`kernel/arch/x86_64/backtrace.cpp`、`kernel/mm/diagnostics.cpp`、`kernel/drivers/ahci/ahci.cpp`、`kernel/gui/gui_init.cpp`、`kernel/test/{test_backtrace,test_kallsyms,test_ahci_write,test_pic_pit,test_scheduler,test_sync_concurrent}.cpp`。
- 不动 mini/（独立 flag 集，留 follow-up 决定是否同步）。
