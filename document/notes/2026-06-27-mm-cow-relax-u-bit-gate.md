# 【回补】2026-06-27 CoW 解析松 U 位门控 — 内核态写 CoW 用户页不再 panic

## 背景

CinuxOS 的 page-fault 处理沿用 F2 懒分配范式([exception_handlers.cpp](../../kernel/arch/x86_64/exception_handlers.cpp)
的 `handle_pf`):缺页靠 demand paging 服务,而**写一个 present 但写保护的页**走
Copy-on-Write 解析,交给 [handle_cow_fault](../../kernel/proc/process_new.cpp)。fork 把父子
共享的页标成 CoW(清 W、置 `FLAG_COW`),任一方写时复制一份独立副本。

原本这条 CoW 解析入口有一个**门控**:

```cpp
if ((err & 0x01) && (err & 0x02) && (err & 0x04))   // present + write + user
```

x86 `#PF` 的 error code:`bit0=present`、`bit1=write`、`bit2=user(U 位,故障来自 ring3)`。
也就是说,原代码**只对「用户态写」解析 CoW**。

## 根因:内核态写 CoW 用户页被漏掉 → panic

CinuxOS 的 syscall 现状是**直接解引用用户指针**,还没有 `copy_to_user` 这一层(那是后续
SMAP 重做的活)。于是内核会**合法地写用户页**。典型场景就在 F10 musl 路径上:

shell 跑起来后 fork 出子进程,**父进程的整个地址空间被标成 CoW**(栈页也是)。父进程随后
`sys_waitpid`,内核要把子进程的退出状态**写回用户态的 `*status`**——这个 status 指针正好
落在父进程那块 fork-CoW 的栈页里。

内核写它 → present + write + **supervisor**(U 位为 0),error code = `0x3`。原门控要求
`err & 0x04`,U 位是 0 → CoW 解析**整段被跳过** → 直接落到下面的 `panic("#PF ...")`。

shell 一跑 fork + waitpid 就炸,卡在 F10 musl 用户程序这条主线上。单核测试里这条路径未必
每次都踩到(取决于 fork 后那块栈页是否被标 CoW),所以一直没被门控暴露。

## 实现(9fba65b,一处门控)

把 U 位从**门控条件**里拿掉,改成对**任意 CPL 的写**都尝试 CoW 解析:

```cpp
// CoW fault: ... Resolve for ANY writer (user OR kernel): CinuxOS syscalls
// directly dereference user pointers (no copy_to_user yet), so the kernel
// legitimately writes CoW user pages -- e.g. waitpid storing *status into the
// parent's fork-CoW'd stack.  handle_cow_fault guards on FLAG_COW, so a genuine
// read-only page (not CoW) still falls through to panic below.
if ((err & 0x01) && (err & 0x02)) {
    if (cinux::proc::handle_cow_fault(fault_addr)) {
        return;
    }
    dump_cow_fail_diagnostic(fault_addr);   // 解析失败打诊断,再落 panic
}
```

### 为什么松门控不会误判

关键在 `handle_cow_fault` **自己有第二道门**([process_new.cpp:88](../../kernel/proc/process_new.cpp)):

```cpp
if (!(pte->raw & FLAG_COW))
    return false;          // 没标 CoW 的真只读页,不解析
```

它读 PTE,只有带 `FLAG_COW` 的页才复制;真只读页(代码段、真 readonly 映射)没有
`FLAG_COW`,返回 `false` → 控制流回到 `handle_pf` 继续往下,最终照旧 `panic`。所以松掉
入口的 U 位**只是让内核态写也能进到这道真正的 CoW 判定**,语义没放宽,无误判、无回归。

## 验证

`run-kernel-test-all` 两 leg(单核 + `-smp 2`)955/0 全绿,`-smp2` AP 回读 PASS,零回归。
shell fork + waitpid 这条 F10 musl 主线路径不再因 CoW 误判 panic。

## 关键教训

- **门控别叠床架屋**。`handle_pf` 入口本来就在拿 error code 分类,U 位门控 + `handle_cow_fault`
  内部 `FLAG_COW` 门控,是两层意思不同的判定叠在一起。入口松一点、把真正的语义判定下沉到
  能看 PTE 的地方,反而更准也更不容易漏路径(内核态写这条路就是被入口的 U 位挡死的)。
- **门控分叉要跟 syscall 模型对齐**。原门控假设「写用户页必来自用户态」——这是有 `copy_to_user`
  的世界。CinuxOS 现在内核直接解引用用户指针,内核态写用户页是**常态**(waitpid/各种 accessor),
  U 位门控就跟实际模型脱节了。后续 SMAP 重做把 accessor 化铺开,内核直接解引用会逐步收敛,
  但在那之前这道松门控是必要兜底。
- **demand-paging 的分叉判据同理**:F2 里 not-present 缺页也用 `err&0x04` 区分 user/kernel
  (kernel-mode 访问用户地址走零页容错,真 user-mode 非法访问走 segfault)。U 位在 CinuxOS 是
  区分「真用户态」vs「内核代访」的分界,但**不能**反过来用它去**否定**一个内核代访的合法
  CoW 写——那正好是本 bug 的错。

## 残留

- 本批只是让 CoW 解析对内核态写放行;`handle_cow_fault` 释放 `old_phys` 仍是**单核正确**,
  线程跨核迁移 mid-CoW 的窗口尚在(代码注释自承,跨核 TLB shootdown 是独立 follow-up,
  见 2026-06-29 fork 复活 saga 笔记)。
- SMAP 重做(accessor 化、撤全局 stac)是后续 P0a–P0g + P3 的弧,会把「内核直接解引用
  用户指针」逐步收进 stac/clac 窗口;届时内核写 CoW 用户页的路径会更规范,但本道门控仍成立。
