# 交接简报：SMP shell fork 后用户态腐蚀（头号未解 bug）

> 给接手的 AI：这是一个**深、难、已花大量精力**的 bug。请先读本文件 + `2026-06-27-f10-smp-cow-tlb-flush-fix.md`（含「⚠️ 更正」段）+ `2026-06-27-shell-launch-smp-investigation.md`。**不要重复已排除的假设**（见下）。
> 分支 `feat/f10-musl`。仓库根 `/home/charliechen/CinuxOS`。

## 目标
修掉 GUI shell（`make run`，-smp 2）启动 musl 程序（fork+execve）时的崩溃。

## 当前状态（已 commit，绿）
**两个修复已落地且 proven necessary**（不要回退、不要重做）：
- `ea4520e` syscall.S sysretq 退出补 `movq 88(%rsp),%rbp`（恢复用户 RBP，原漏恢复，ABI 违规）。
- `6a7beb9` fork.cpp L279 + clone.cpp L115 的 CoW 页表遍历后加 `flush_tlb_all()`（原 copy_page_table_level 改父在用 PTE writable→CoW 后不刷 TLB，父写穿透陈旧 TLB 到共享页）。
- **实证有效**：`tools/musl/forktest.c` ITERS=1（单 fork）-smp 2 **3/3 races=0**（CoW 隔离正确）；未修时 run1 races=1（偶发）。
- 默认 `run-kernel-test` 954/0 + `run-kernel-test-smp` 954/0 无回归。

**但不够**：GUI shell 修后**仍崩**。

## 崩溃症状（三份修后日志，仓库根 `log.txt`/`log2.txt`/`log3.txt`，09:22 生成，晚于 09:14 修复 commit）
- **log.txt**：shell fork 子(tid=7) → 子 CoW 一次 → 子进 syscall → `syscall_dispatch` 末尾 `ret`（RIP `0xFFFFFFFF81004D67`）**#GP**（error 0），弹出的返回地址是垃圾、saved rbp=0 → **子内核栈 pt_regs 帧被砸**。
- **log2.txt**：子调 `sys_execve` 时 **path=0x0（NULL）** → EINVAL → shell 重 fork → fork 炸弹。串口字节交错（`[PROC] fork...parent[_CpiOWd]=1re`）证明**子在 fork() 返回前就在 AP 上跑**（`Scheduler::add_task`→`wake_idle_ap`，fork.cpp:322）。
- **log3.txt**：`handle_pf` 的 klog 路径**嵌套 #PF**；栈里 ASCII（"rne-mode"，疑似 "kernel-mode" 串）+ `0xf00f00f0` 填充 = 内存被字符串/缓冲写花。

## headless 复现（关键）
`tools/musl/forktest.c`（裸 SYS_fork 57 循环，装成 /hello 让 ring-3 smoke 在 -smp 2 起）：**ITERS=1 通（races=0），ITERS≥2 崩**（父 tid 在第 2 次 sys_fork 返回处 segfault，rbp=0x13）。单 CPU 也崩（非 SMP 专属）。

构建/跑（KVM 才有真 TLB/SMP；本机 /dev/kvm 可用）：
```bash
cd /home/charliechen/CinuxOS
SYSROOT=build/musl-sysroot; CB="$(gcc -print-file-name=crtbeginS.o)"; CE="$(gcc -print-file-name=crtendS.o)"
gcc -static -nostdlib -no-pie -DFORKTEST_ITERS=2 -L"$SYSROOT/lib" "$SYSROOT/lib/Scrt1.o" "$SYSROOT/lib/crti.o" "$CB" tools/musl/forktest.c -lc -lgcc "$CE" "$SYSROOT/lib/crtn.o" -o build/musl/ft2
scripts/create_ext2_disk.sh build/ext2_ft2.img build/user/shell build/musl/ft2 >/dev/null 2>&1
cmake --build build --target test-image -j$(nproc)
timeout 100 qemu-system-x86_64 -m 8G -serial stdio -no-reboot -debugcon file:build/debug.log -global isa-debugcon.iobase=0xE9 -accel kvm -cpu max -vnc :0 -usb -device isa-debug-exit,iobase=0xf4,iosize=0x04 -device ahci,id=ahci -drive file=build/ahci_test.img,format=raw,if=none,id=ahci-disk -device ide-hd,drive=ahci-disk,bus=ahci.0 -drive file=build/ext2_ft2.img,format=raw,if=none,id=ext2-disk -device ide-hd,drive=ext2-disk,bus=ahci.1 -drive file=build/cinux_test.img,format=raw,index=0,media=disk
# 看 segfault tid ... rip=0x401182 rbp=0x13
```
跑 GUI 真实路径：`timeout 40 cmake --build build --target run -j$(nproc)`（-smp 2 GUI；点 Shell 图标，敲 /hello）。

## 已排除的假设（**别再查这些**，省时间）
1. **IST=0 栈别名**（6-agent workflow 的主结论，**不成立**）：#PF 注册 IST=0（idt.cpp:112），kernel→kernel 同特权中断用**当前 RSP**，不撞 `kernel_stack_top`。syscall_entry（syscall.S:61 `movq %gs:0,%rsp`）的 pt_regs 与嵌套 #PF 帧不别名。给 #PF 加 IST 是非标准且不治本。
2. **内核 pt_regs 帧被改写**（我加过 canary 证伪）：syscall_entry 入口存用户 RBP→`%gs:0x28`，出口比 frame+88，**未触发**。帧没被改写。
3. **sys_yield 破坏 callee 寄存器**（`tools/musl/calleetest.c` 证伪）：RBX/R12-R15 跨 yield 2000/0 全保持。
4. **PMM/vaddr 别名**（workflow 证伪）：alloc_stack_vaddr 单调原子；alloc_page/alloc_pages 持锁；内核栈不进 CoW（fork.cpp:57 跳 !FLAG_USER）。
5. **mapcount 双释放/早释放重映射**（workflow 证伪）：fork.cpp:91 每叶每 fork 一次 inc；free_subtree 跳数据页；只会泄漏不会早释放。

## 当前最佳理解（待验证）
崩溃是**用户态控制流被劫持**：父（或子）**用户栈里的返回地址被写花**，跳到 `0x401182`（sys_fork 中段，绕过 prologue）→ rbp=垃圾 `0x13` → segfault（或子 execve path=0x0）。shell 启动期已 demand-page 一堆页，它的 fork 实质 = forktest 的「第 2 次」fork（AS 已被 CoW 动过），所以一上来就踩。

## 候选根因（按概率）
1. **`handle_cow_fault` 跨核释放 old_phys**（process_new.cpp:115-120 自承认注释）：父+子跨 CPU 同页并发 CoW，一核 `mapcount_dec_and_test→0→free_page(old_phys)`，另一核还在 `memcpy` 读 old_phys → 读到已释放+被 PMM 复用的页 → CoW 拷贝带垃圾 → 用户栈/数据花。**解释 -smp 2 shell 崩**。但不解释单 CPU forktest ITERS≥2（无并发）。
2. **单 CPU forktest ITERS≥2**：无并发、IF=0、无 AP，却仍崩。疑 fork 后某用户页状态/CoW 拷贝路径在「AS 已被 CoW 动过」时出错。未钉死。
3. 子在 fork() 返回前在 AP 上跑（log2 实证），与父并发碰共享页——放大 #1。

## 关键文件
- `kernel/proc/fork.cpp`（copy_page_table_level L49-102；fork() L108-325；栈 memcpy L214；ctx.rsp L221；add_task L322）
- `kernel/proc/clone.cpp`（cow_clone_address_space L72-120；同 bug 同修）
- `kernel/proc/process_new.cpp`（handle_cow_fault L72-127，**释放竞态 L115-120**；handle_pf 调它）
- `kernel/arch/x86_64/exception_handlers.cpp`（handle_pf L272-459；segfault/log 路径 L380）
- `kernel/arch/x86_64/syscall.S`（syscall_entry；frame+80 RBX / frame+88 RBP；已加 rbp 恢复）
- `kernel/arch/x86_64/interrupts.S`（PF/IRQ entry 存/恢复全 GPR 含 R12-15）
- `kernel/mm/pmm.cpp`（mapcount 原子 L229-243；alloc_page_locked 无锁 L157）
- `kernel/proc/scheduler.cpp`（add_task→wake_idle_ap L212；update_syscall_stack L284）
- `tools/musl/forktest.c`、`tools/musl/calleetest.c`（复现器/诊断）

## 建议下一步（接手干）
1. **先试低风险「不释放」消 UAF**：handle_cow_fault 里把 `if (mapcount_dec_and_test) free_page` 改成只 dec 不 free（接受泄漏），看 -smp 2 shell / forktest ITERS≥2 是否稳。稳了 → 确认 #1；不稳 → 不是它。
2. 若 #1 无效：给用户栈加 canary（forktest 里每个函数 prologue 后写 magic 到栈尾，crash 时 dump），或 dump crash 时父用户栈内容，定位**哪个用户页被花**（哪条返回地址变垃圾）。
3. 单 CPU forktest ITERS≥2 路径单独查（无并发，应更易钉）：可能 copy_page_table_level 对「已 CoW 页」二次 fork 的某状态处理错，或 wait4/reap 后某页状态。
4. 复现用 KVM（真 TLB/SMP）；单 CPU 也复现（`qemu` 去 `-smp 2`）。

## 已知周边（别误判为新 bug）
- harness ring-3 smoke 的 worker 是无 AS 内核线程，fork 跳过 CoW（fork.cpp:226 `if(addr_space!=nullptr)`）→ **run-kernel-test(-smp) 抓不到此 bug**。必须 user task（有 AS）fork 才触发；forktest 装成 /hello 让 smoke 的子（user task）自己 fork 才命中。
- printf/stdout FILE segfault（musl __stdout_write，F10 批6 残留）与本 bug 无关；raw write() 正常。
- CINUX_MUSL_HELLO_SMOKE 默认 OFF，但本 build 缓存里是 ON（跑 forktest 要 ON）。

## 提交规范
`<type>(<scope>): <中文简述>`，纯描述，**不带 Co-Authored-By / AI 署名**。绿才提交（`timeout 40 cmake --build build --target run-kernel-test -j$(nproc)`）。push/PR 由用户控制。改公共接口补全量 `cmake --build build`。详见 `CLAUDE.md` + `document/ai/DIRECTIVES.md` + `CODING-TASTE.md`。
