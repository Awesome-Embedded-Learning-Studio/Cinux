# F-EXTABLE 批1:__ex_table 基建 + search/sort(零行为变)

**日期**:2026-06-29
**里程碑**:F-EXTABLE(Linux 风格 exception table,横切,SMAP follow-up #2 阶段 1)
**分支**:`feat/f-extable`(从 main `7f846c8`)
**commit**:`04faecd`

## 背景

CinuxOS 的 user accessor(`copy_to_user`/`copy_from_user`,`user_access.hpp`)靠 **demand-page 契约**而非真 fault-recovery:`user_access.hpp:19-25` 注释明说「no exception-table infrastructure... rely on the demand-paging contract」。后果:

- 拷贝中途 `!P` 用户页被 PF handler demand-page zero-page 静默映射,accessor 读到 0 返 `true`(成功)——**掩盖用户传坏指针的 bug**;
- 真不可映射 / SMAP 违法地址 → `panic` → `fatal_halt`(`cli;hlt` 死循环);
- accessor `bool` 接口只能反映 `access_ok` 预检,**永远无法表达 mid-copy fault** → 「accessor 解引用非法用户指针应返 `-EFAULT`」的**真负测试根本写不了**(kernel violation 必 panic)。

F-EXTABLE 建 RIP-based `__ex_table`(Linux uaccess 范式):fault 指令 RIP 命中注解 accessor 指令时,PF handler 改 `frame->rip` 跳 fixup、accessor 返 `-EFAULT`,解锁真负测试。

## 范围栅栏(硬边界)

**只做阶段 1 = extable 基建 + accessor 重写 + 负测试**(批 1-4)。extable 是 RIP-based 精准拦截,只在「内核态 fault(`cs&3==0`)+ RIP 命中」时早返。**绝不改 demand-page 对用户态缺页的宽松逻辑**——那是 F2 lazy-allocation 范式(memory `f2-lazy-allocation-paradigm` 标 M5「高风险,漏判→shell halt」),撤它留阶段 2 / F2-M5。

## 批 1 做了什么(零行为变)

1. **[linker.ld](../../kernel/linker.ld)**:加 `__ex_table` section(仿 `.init_array`,`AT(ADDR-KERNEL_VMA)` + `ALIGN(8)` + `KEEP` 防 gc-sections + `__start___ex_table`/`__stop___ex_table` 符号)。放 `.init_array` 后、`.initrd` 前。
2. **[extable.hpp](../../kernel/arch/x86_64/extable.hpp)(新)**:
   - `ExceptionTableEntry{fault_rip, fixup_rip}`(16B,`static_assert`)
   - `extable_search(begin, end, rip)` 二分查找(**纯函数 host 可测**)
   - `extable_sort(begin, end)` 插入排序(freestanding 无 qsort,**纯函数 host 可测**)
   - kernel wrapper `search_exception_tables(rip)` / `sort_extable()`(引用 linker 符号)
   - `_ASM_EXTABLE(fault_lbl, fixup_lbl)` 宏(`.pushsection __ex_table` + 两 `.quad`,数值标签 `1b`/`2b`)
3. **[main.cpp](../../kernel/main.cpp) / [main_test.cpp](../../kernel/test/main_test.cpp)**:`irq_init` / `g_idt.init` 后调 `sort_extable`(IDT up + 中断未开,空表 no-op)。
4. **[test_extable.cpp](../../test/unit/test_extable.cpp)(新 host 单测)**:7 例覆盖 sort 排序/稳定/空单元素 + search 命中头中尾/miss gap-below-above/空/单元素。

## 设计要点

- **纯函数 / wrapper 分离**:`extable_search`/`extable_sort` 接 `[begin, end)` 迭代器,host 用本地数组测;wrapper 引用 `__start/stop___ex_table`。host include `extable.hpp` 但不调 wrapper,故不链接缺符号 → host 单测直接验证二分/排序正确性。
- **section 名 `__ex_table`**(不带前导点):对齐 Linux,避免 ld 通配符 `*(.__ex_table)` 的点号歧义。符号 `__start___ex_table` / `__stop___ex_table`(Linux `__start_` + section 名惯例)。
- **`extern "C"` 声明 linker 符号**:保符号名无 C++ mangling,匹配 linker 标签。表声明非 const(`sort_extable` 启动原地排序;section 落在 `.data` 可写空间)。
- **空表零行为变**:批 1 不注解任何 accessor,`__ex_table` 空,`search` 必 nullptr,`sort` 是 no-op。

## 验证

- host 单测 `test_extable`:**7 passed, 0 failed**。
- `nm build/kernel/big/big_kernel`:`__start___ex_table == __stop___ex_table == 0xffffffff81046cd0`(同地址 = 空表,标记 `D` 可写)。
- `run-kernel-test-all` 两 leg:**960 passed, 0 failed** + AP1 机制回读(cr4=0x300620 含 SMEP/SMAP / lstar 非零 / efer=0xd01 NXE)+ smoke 20/20 + 零 panic。

## GOTCHA

- **test_framework.h 的 `TEST_FRAMEWORK_IMPL` 不提供 `main`**:只定义全局变量(`_test_registry` 等),每个 host 单测自写 `int main() { RUN_ALL_TESTS(); return _tests_failed > 0 ? 1 : 0; }`(对齐 `test_font`/`test_pic`)。初版 `test_extable` 漏 main → `undefined reference to main`。
- **extable.hpp 自包含**(只依赖 `<stdint.h>`/`<stddef.h>`):host 单测能直接 include(无 kernel 依赖链)。

## 下批

- 批 2:`copy_to_user`/`copy_from_user` 改 inline asm(`rep movsb` + extable + fixup clac),**不碰 PF**。put/get 保 wrapper。验证 `nm` 表非空 + 反汇编审 fixup clac + ring-3 smoke。
