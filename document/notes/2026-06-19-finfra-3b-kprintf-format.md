# F-INFRA I-3b — kprintf format 属性 + 修真实格式不匹配

> 日期 2026-06-19 · F-INFRA Tier0 批 I-3b · 分支 `feat/finfra`

## 背景
R2 验证补丁：I-3 加的 `-Wformat=2` 当时管不到 kprintf——[kprintf.hpp](../../kernel/lib/kprintf.hpp) 的 `kprintf/kvprintf/kpanic` 无 `__attribute__((format))`，编译器把 fmt 当不透明串，零检查。本批补属性，让 `-Wformat=2` 真正生效。

## 目标
给 kprintf 家族加 `format(printf)` 属性，修暴露的真实格式不匹配，达成 format 安全零警告。

## 设计/决策
- 加属性：`kprintf`/`kpanic` → `format(printf, 1, 2)`；`kvprintf` → `format(printf, 1, 0)`（va_list 版，只查 fmt 串本身）。GCC+Clang 都支持 `__attribute__`。
- **属性与 kprintf 实际行为对齐**：读 [vkprintf_impl.hpp](../../kernel/lib/private/vkprintf_impl.hpp) 确认——`%x/%u/%d` 无 `l` 修饰时 `va_arg(unsigned int)`（32 位），与标准 printf 一致；`%lu/%lx/%ld` 读 64 位。FO 笔记"％x 是 uint64"不精确。故属性**不是**误报源，而是真能抓「64 位值传 %u/%x 无 l → kprintf 读 32 位 → 静默截断」。

## 陷阱
- **`-Wformat=2` 只暴露 21 处**（非 feared 的 ~662）——代码库大部分传参本就正确或带 `(unsigned)` cast。21 处是漏网的。
- **`0x%p` 双重 "0x" 潜在 cosmetic bug**：kprintf `%p` 自动前缀 "0x"，故格式串里再写 `0x%p` 会打 "0x0x..."。[ahci.cpp:56](../../kernel/drivers/ahci/ahci.cpp)、[pci.cpp:164](../../kernel/drivers/pci/pci.cpp) 把 uint64 地址传 `%p`，改 `0x%p`→`0x%lx` 既修类型告警又修双前缀。
- **replace_all 误伤**：exception_handlers.cpp 的 `tid=%u` replace_all→`%lu`，误改了 2 处本就用 `static_cast<unsigned>(...->tid)` 配 `%u` 的站点（102/283，参数是 unsigned int 非 uint64）→ 新告警。用 cast 上下文精确回退这两处（其余 `tid=%u` 配 uint64 才改 `%lu`）。

## 清理（21 处，全真实类型不匹配）
- `%u`+uint64_t → `%lu`（kprintf 64 位）：scheduler x6（tid）、pmm（MB 统计）、ramdisk x3（size/offset/bytes）、task_builder x2（page/tid）、exception_handlers（cur->tid）、clone、fork、init（self->tid）。
- `0x%p`+uint64 → `0x%lx`：ahci（BAR5 phys/virt）、pci（BAR5）。
- 回退 2 处 `static_cast<unsigned>`+`%u`（exception_handlers 102/283）。

## 验证
- `cmake --build build --target run-kernel-test` → **零 format 警告、零任何警告**。
- `timeout 40 ... run-kernel-test` → **840/0**。

## 文件
- 改：`kernel/lib/kprintf.hpp`（属性）、`kernel/proc/{scheduler,task_builder,clone,fork,init}.cpp`、`kernel/arch/x86_64/exception_handlers.cpp`、`kernel/mm/pmm.cpp`、`kernel/fs/ramdisk.cpp`、`kernel/drivers/{ahci/ahci,pci/pci}.cpp`。
