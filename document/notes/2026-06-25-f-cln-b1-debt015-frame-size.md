# F-CLN 批1 — DEBT-015 syscall 栈帧关债 — 2026-06-25

> DEBT-015（syscall handler 栈帧过大）闭环。核实 + 修残余 + 启用门禁。

## 核实发现
debt.md 登记时（2026-06-20 F-QA Q1-1）列 9 个 syscall handler 放 `char[PATH_MAX]` 栈缓冲。核实：**8 个早年已改 `PathBuf` 堆**（[path.cpp:19](../../kernel/fs/path.cpp#L19) 注释 "was char[PATH_MAX] on the stack"，`grep '[PATH_MAX]' kernel/syscall/` 空）——debt.md 滞后。**残余 1 个：`sys_dmesg` `LogEntry entries[16]`**（LogEntry 272B × 16 = 4.4KB 栈帧）。

## 修复
[sys_dmesg.cpp:87](../../kernel/syscall/sys_dmesg.cpp#L87) `LogEntry entries[16]`（栈）→ `LogEntry* entries = new LogEntry[kMaxEntries]`（堆，operator new[] → kmalloc via crt_stub，F2-M7b slab）；loop 体零改（指针下标同数组下标）；`delete[] entries` 在单 return 前。nullptr 检查返 `-ENOMEM`。

## 门禁
`-Wframe-larger-than=1024` 加 `big_kernel_common` PUBLIC（警告级）。
**GCC 技术限制**：`-Werror=frame-larger-than` GCC 拒绝（"no option -Wframe-larger-than"，带参 warning 名不支持 -Werror= 升级）→ 硬门禁不可行，降为 warning + 审计 grep。
`big_kernel_test` 加 `-Wno-frame-larger-than`（test fixture 栈大是设计：LogEntry[16]/AHCI 请求数组/ramdisk 缓冲，非生产核栈 + IRQ 嵌套）。

## 验证
- big_kernel 生产编译：`-Wframe-larger-than=1024` **零 frame warning + 零 error**（sys_dmesg 改堆后生产栈帧全 <1024）
- run-kernel-test **931/0** 绿
- 残余命中全在 test/（test_apic/test_ahci/test_ramdisk/test_vfs_syscall/test_xhci/test_mouse_event），test 不在生产核栈，-Wno 免除

## 产出
- sys_dmesg.cpp（栈→堆）+ CMakeLists.txt（门禁 warning + test -Wno + 注释订正 DEFERRED→已闭环）+ debt.md DEBT-015 ✅ + PLAN 批1 ✅

下个：批2 DEBT-016 test framework ASSERT_OK + 清 32 处 ErrorOr 忽略。
