# F-EXTABLE 批3:handle_pf 接线 extable 查找(内核态 accessor fault 走 fixup)

**日期**:2026-06-29
**里程碑**:F-EXTABLE(批 3)
**分支**:`feat/f-extable`
**commit**:`c09e6cf`

## 做了什么

[exception_handlers.cpp](../../kernel/arch/x86_64/exception_handlers.cpp) 的 `handle_pf` 读 CR2 后、`capture_first_pf`/栈守卫/demand-page/CoW **之前**加 extable 查找:

```cpp
if ((frame->cs & 0x03) == 0) {                       // 内核态门
    if (const auto* entry = cinux::arch::search_exception_tables(frame->rip)) {
        frame->rip = entry->fixup_rip;                // iretq 回 fixup(clac + ok=false)
        return;
    }
}
```

内核态 fault(`cs&3==0`)+ RIP 命中 extable 注解的 accessor(`copy_to`/`from_user` rep movsb)→ 改 `frame->rip = fixup_rip` + return。iretq 回 fixup(clac + xorl ok=false),accessor 返 false(caller 返 `-EFAULT`)。

## 范围栅栏

- **用户态 fault(`cs&3!=0`)跳过**:走原 demand-page 逐字节不变(F2 lazy-allocation 范式)。
- **extable 只拦 accessor RIP**:正常内核 fault(demand-page/CoW/栈守卫/NULL-deref)的 RIP 非 accessor,查表 miss → 原逻辑不变。

## 设计要点

- **查表在 demand-page/CoW/栈守卫前**:确保 accessor fault 优先走 extable(否则 near-NULL accessor fault 仍被 demand-page 掩盖或栈守卫 panic)。
- **frame->rip/cs 可写**:`InterruptFrame`(idt.hpp:71-78)`rip`@128/`cs`@136 是普通可写成员,offset 由 static_assert 锁;iretq 弹改后的 frame->rip。
- **`cs&3` 判内核态**:对齐 #GP handler 既有写法(不用 `err&0x04` U 位)。
- **不碰 frame->rax**:accessor asm 经 `"+r"(ok)` 输出约束,fixup xorl 清 ok 寄存器(可能非 rax);iretq 恢复 frame 寄存器到 accessor 上下文,fixup 在其上跑。

## 验证

零回归(查表 inert):run-kernel-test-all 两 leg **960 passed, 0 failed** + smoke 20/20 + accessor 正常路径(musl hello 经 accessor 写)。accessor fault 真走 extable 返 `-EFAULT` 的**实证**留批 4 RSVD/负测。

## 下批

- 批 4:RSVD 负测(或 Fallback A 未映射址)证 accessor fault 走 extable 返 false 非 panic;host search/sort 单测;ROADMAP/PLAN/notes/memory 收尾。
