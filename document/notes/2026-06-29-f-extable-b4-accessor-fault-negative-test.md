# F-EXTABLE 批4:accessor fault 负测(extable 真拦实证)+ 里程碑收官

**日期**:2026-06-29
**里程碑**:F-EXTABLE(批 4,收官)
**分支**:`feat/f-extable`
**commit**:`0f60f47`

## 做了什么

既有 [test_user_ptr.cpp](../../kernel/test/test_user_ptr.cpp) 加 `namespace test_extable` + 2 真负测:

- `test_copy_from_unmapped_returns_false`:`copy_from_user` 解引用未映射用户址 → rep movsb #PF → handle_pf(B3)extable 查 → fixup → 返 false。
- `test_copy_to_unmapped_returns_false`:对称(dst 写未映射址)。

注册到既有 `run_user_ptr_tests`(F-EXTABLE section)。host search/sort 单测已在批 1(`test_extable` 7 例)。

## 验证(实证 extable 真拦)

run-kernel-test-all 两 leg **962 passed, 0 failed**(+2 负测)+ smoke 20/20。负测 **PASS**(返 false 非 panic),证 B3 extable 真拦 accessor fault(fault RIP 命中 `__ex_table` → 改 `frame->rip` → fixup clac+ok=false → `-EFAULT`),不走 demand-page/panic。

## GOTCHA(测试坑)

**负测址要避开 identity/direct-map 覆盖区**:初版用 `0x40000000`(1GB 用户址)返 true(2 failed)——被 test kernel 的 identity/direct-map 映射(物理 RAM 覆盖到 1GB+),rep movsb 成功读不 fault,extable 没机会拦。换 **`0x7000000000`**(481GB,>物理 RAM + mmap 4-24GB 范围)才真未映射 → fault → extable 拦。教训:accessor fault 负测址必须确认未被内核映射(identity map / direct-map / mmap / brk / 栈),用高位用户址避开。

(原 plan RSVD-bit 主方案未用——Fallback A 未映射址 + B3 extable 在 demand-page 前已足够证 extable 工作,不需手映 RSVD PTE。)

## F-EXTABLE 里程碑收官(批 1-4 全 ✅)

- 批 1(`04faecd`):`__ex_table` section + `extable.hpp`(search/sort/`_ASM_EXTABLE`)+ 接线 + host 单测。
- 批 2(`77ff9cf`):`copy_to/from_user` inline asm + extable 注解 + fixup clac。
- 批 3(`c09e6cf`):`handle_pf` extable 查找(内核态门)。
- 批 4(`0f60f47`):accessor fault 负测(实证)。

验证矩阵:run-kernel-test-all 两 leg **962/0** + host `test_extable` 7/0 + smoke 20/20 + 反汇编两路 clac + nm `__ex_table` 21 项。

**范围栅栏达成**:extable 只拦内核态 accessor RIP,用户态 demand-page(F2 lazy-allocation)逐字节不变。撤宽松 demand-page(阶段 2)留 F2-M5 高风险里程碑。

分支 `feat/f-extable`(8 commit 待 push,push 归用户)。
