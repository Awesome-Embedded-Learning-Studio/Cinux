# SMAP (B) — gui_worker「空指针」真相:stack canary 错配(PF 门控反挖)

**日期**:2026-06-28　**分支**:feat/f10-tty-dyn
**提交**:4c534a9
**验证**:run-kernel-test-all 两 leg 960/0 + ring-3 20/20 + 无 PANIC

## 背景:(B) 难复现+难定位的 gui_worker 崩溃
交接 (B):`gui_worker`(tid=6)Region::add CR2=0x28「空指针」偶发崩,PANIC read kernel @0x28。用户多次跑没复现。让「顺手看看」。

## 诊断链(教科书级,PF 门控反挖出真因)
1. **加 PF <0x1000 门控**(对齐 Linux "kernel NULL pointer dereference" oops,不 demand-page 掩盖)→ **立刻**抓到 `test_pump_flush → pump` 的 #PF 0x28(此 test 之前一直假 PASS)。
2. **objdump RIP = `mov rax, fs:0x28`** —— stack canary(GCC `-fstack-protector` TLS offset 0x28),**根本不是 nullptr deref**!
3. cgui(submodule)编译没 `-mstack-protector-guard=global` → canary 默认 TLS(`fs:0x28`)→ kernel 上下文无 TLS canary slot → #PF 0x28 → 旧 PF handler 对 kernel-mode/no-task user-addr PF **宽松 demand-page zero page**(exception_handlers.cpp:315)掩盖(读 0,canary 0==0 假过)→ `test_pump_flush` 长期假 PASS → (B) gui_worker 偶发真崩在不知所云处。

**这解释 (B) 难复现+难定位**:canary 读失败被 PF handler 兜住(映射 zero page,读到 0,程序继续),崩点(0x28)根本不是 root cause。CR2=0x28 看着像 nullptr+0x28,实际是 `fs:0x28` canary TLS offset。

## Fix
- **cgui `-mstack-protector-guard=global`**(CMakeLists add_subdirectory 后 `target_compile_options(cinux-gui PRIVATE ...)`):canary 读 kernel 全局 `__stack_chk_guard`(boot.S seed),对齐 kernel(kernel/CMakeLists.txt:26 的 `-mstack-protector-guard=global`;cgui 在 kernel **之前** add_subdirectory,不继承)。
- **PF <0x1000 门控**(exception_handlers.cpp:317):kernel-mode/no-task 对 nullptr 区(<0x1000)的 user-addr PF `panic(frame, ...)` 带 RIP/backtrace(对齐 Linux oops),不再 demand-page 掩盖。以后任何被掩盖的 nullptr/canary 读失败立即炸在 deref 点(就是它第一时间反挖出 (B) 真因)。

## 教训
- **PF handler 宽松 demand-page 是双刃剑**:F2 懒分配(任何 not-present PF demand-page)掩盖了 kernel nullptr/canary 读失败(映射 zero page,读到 0,程序继续崩在别处)。<0x1000 门控堵了这个洞(kernel 态解引用 nullptr 区 100% bug,立即 panic 带栈)。
- **submodule 编译要对齐 kernel stack-protector-guard**:cgui 没 `-mstack-protector-guard=global` → TLS canary(fs:0x28)错配。parent add_subdirectory 后给子 target 补 flag(或 cgui CMake 自己加)。
- **PF 门控反过来是诊断利器**:加门控后,所有被 demand-page 掩盖的隐藏 deref bug 立即暴露(带栈)。(B) 不是唯一,test_pump_flush 也是长期假 PASS。

## 关键文件
- CMakeLists.txt:cinux-gui `-mstack-protector-guard=global`(add_subdirectory 后)。
- kernel/arch/x86_64/exception_handlers.cpp:PF handler <0x1000 nullptr 区门控(panic 带 frame/backtrace)。
- 诊断工具:objdump RIP 看指令(`mov rax, fs:0x28` 识别 canary)+ addr2line + PF panic dump(RAX/RBX=0 nullptr)。
