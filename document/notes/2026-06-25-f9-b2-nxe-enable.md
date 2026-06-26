# F9 批2:开 EFER.NXE + NX 全面生效(W^X)

> 2026-06-25 · F9 安全机制 · 批2 · `feat/f9-security` · commit(本次)
> F9 核心:No-Execute 位真正启用,用户栈/heap/非可执行文件页不可执行。

## 背景

批1 把 sigreturn 从栈上可执行代码救出后,开 NXE 的最后障碍清除。批2 拨下 EFER.NXE 总开关,并补全所有 "deferred NX" 逻辑——此前五处代码注释「NXE off until F9」都在等这一刻。

## 改动

- **[usermode.S](../../kernel/arch/x86_64/usermode.S)**:设 EFER 时 `orq $1`(SCE)→ `orq $(1 | (1<<11))`(SCE + NXE)。BSP 和每个 AP 都走 `usermode_init_asm`,**一处覆盖所有核**(避开 BSP/AP MSR 分歧的 bug 类)。
- **[exception_handlers.cpp](../../kernel/arch/x86_64/exception_handlers.cpp)** PF handler:查到 VMA 后,`!has_flag(Exec)` → 加 FLAG_NX。一条逻辑覆盖三条路径:
  - 匿名/栈/heap fault(`map_flags`):栈 VMA(Read|Write|Stack,无 Exec)→ 栈 NX ✓
  - 文件 demand-read(`fflags`):非 Exec 文件页 → NX
  - ELF .text(Exec VMA)→ 保持可执行
- **[sys_mmap.cpp](../../kernel/syscall/sys_mmap.cpp)**:`!(prot & PROT_EXEC)` → FLAG_NX(之前 deferred)。
- **[address_space.cpp](../../kernel/mm/address_space.cpp) / [execve.cpp](../../kernel/proc/execve.cpp)**:huge-page 注释清理(去掉过时的「NXE off」描述)。

## 关键决策

- **NXE 放 usermode.S,不另起 C 函数**:BSP+AP 都走 usermode_init_asm,单一改动点全核生效(对齐 memory「AP 须对齐 BSP 全部 MSR」教训)。
- **不加 CPUID gate**:NX 是 x86_64 baseline(长模式用 PAE 页表,bit-63 NX 在 NXE 关时是 reserved 位),任何能跑 x86_64 的 CPU/QEMU 都支持。无条件开,注释说明。

## GOTCHA

- **execve 早已设 FLAG_NX**([execve.cpp:256](../../kernel/proc/execve.cpp),非 PF_X 段),但 NXE 关时 bit 63 是 reserved 位——理论上设了会 reserved-bit #PF,可测试一直绿(数据段未被当代码执行 / 或恰好没踩到)。开 NXE 后这个设置「转正」合法生效,矛盾消解。开闸本身安全:内核页从未标 NX,仍是可执行。

## 验证

- run-kernel-test **931/0**(开 NXE 后无 instruction-fetch #PF / reserved-bit fault)。
- `cmake --build build --target run` 冒烟:GUI Desktop / shell / xHCI keyboard 全正常启动,**零 panic / #PF / SIGSEGV**。NX 真正生效且不破坏真程序。

## 下一步

批3 SMEP(CR4 bit 20)+ 批4 SMAP(CR4 bit 21 + stac/clac,硬骨头)。
