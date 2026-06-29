# F-EXTABLE 批2:accessor 改 inline asm + extable 注解(不碰 PF)

**日期**:2026-06-29
**里程碑**:F-EXTABLE(批 2)
**分支**:`feat/f-extable`
**commit**:`77ff9cf`

## 做了什么

[user_access.hpp](../../kernel/arch/x86_64/user_access.hpp) 的 `copy_to_user` / `copy_from_user` **kernel 分支**(`#else`,非 host)从 C 字节循环改成 inline asm:

```cpp
asm volatile(
    "stac\n"
    "1: rep movsb\n"
    "   clac\n"
    "   jmp 3f\n"
    "2: clac\n"                      // fixup: AC 仍=1,关窗 + ok=false
    "   xorl %k[ok], %k[ok]\n"
    "3:\n" _ASM_EXTABLE(1b, 2b)
    : [ok] "+r"(ok), "+c"(n), "+D"(dst), "+S"(src)
    :
    : "memory");
```

- `rep movsb` 是单 fault 点(fault RIP = `1:`),`_ASM_EXTABLE(1b,2b)` 注解 → fixup `2:`(clac + ok=false)。
- **host 分支保持 C 字节循环不变**(`CINUX_HOST_TEST`,无 SMAP/无 #PF,零回归)。
- `put_user`/`get_user` 保 wrapper(调 `copy_*`)。
- 更新 `user_access.hpp:19-27` 注释(删「no extable」,改述 F-EXTABLE 语义)。

## 设计要点

- **fixup 显式 clac**:fault 发生在 `stac` 之后(AC=1),fixup `2:` 必须 clac 关 AC,否则 AC 泄漏(SMAP 旁路)。success 路径(`1:` 后)也 clac。反汇编确认两路 clac。
- **`%k[ok]` 32-bit 清零**:`xorl %k[ok],%k[ok]` 清 ok 寄存器低 32 位(写 32 位寄存器高 32 位零扩展)= ok=false。bool ok 用 `"+r"`(通用寄存器),`%k` 取 32-bit 形式。
- **rep movsb 单 fault 点**:整条拷贝一条指令,fault RIP 精确指向 `1:`(rep 前缀地址)。extable 表项 fault_rip=`1b`。
- **不接线 PF**(本批):accessor 注解 extable,但 `handle_pf` 还没查表(批 3)。故 accessor fault 仍走原 demand-page/panic。本批验证 accessor **正常路径**(合法指针)零回归。

## GOTCHA

- **GCC asm clobber 须三冒号**:`asm(模板 : output : input : clobber)`。漏 input 冒号写成 `: output : "memory"` 会把 `"memory"` 当 input operand(无对应变量),报 `expected '(' before ')'` cascade 错。正确:`: output : : "memory"`(空 input + memory clobber)。初版踩坑,最小复现定位(非 clangd 误报)。
- **`_ASM_EXTABLE` 宏字符串拼接**:`g++ -E` 预处理确认宏正确展开为 `.pushsection __ex_table,"a"\n ...` 字符串序列,与 asm 模板拼接。clangd 静态分析不展开宏,误报 `Expected '(' after 'asm operand'`——以 GCC 编译为准。
- **nm `__ex_table` 标 R 但 sort 安全**:批 2 注解后 section 非空,nm 标 R(read-only section 属性)。但 `sort_extable` 在 `kernel_main` line 131(VMM init line 153 **之前**),初期页表 RW,sort 写 `__ex_table` 不炸——与 Linux `sort_extable` 在 `mark_rodata_ro` 前同理。run-kernel-test-all 两 leg 绿确认。

## 验证

- 反汇编(`big_kernel`):`sys_read`/`sys_write`/`sys_getdents` 等内联点见 `stac → rep movsb → clac → jmp`(success)+ `clac → xor`(fixup),**两路 clac** ✓。
- `nm`:`__start___ex_table=0xffffffff81046cd0` / `__stop___ex_table=0xffffffff81046e20`,section `0x150` 字节 = **21 项**(注解生效)。
- `run-kernel-test-all` 两 leg:**960 passed, 0 failed** + smoke 20/20 + musl hello(经 accessor 写串口)。

## 下批

- 批 3:`handle_pf` 顶部加 extable 查找(内核态门 `cs&3==0` + `search_exception_tables(frame->rip)` 命中改 `frame->rip` return)。批 3 后 accessor fault 真正走 extable 返 -EFAULT。
