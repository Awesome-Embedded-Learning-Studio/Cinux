# FO — 可观测性/调试基建(完成)

> 日期 2026-06-18 · 完成记录
> 分支 `feat/fo-observability`(6 commit,待 PR)。横切里程碑,插 F2 与 F3 间。

## 背景

F2 的 15 个 GOTCHA 大量靠临时加 `kprintf` 诊断定位(nested-KVM EPT 振荡、direct-map walk 错乱、slab 陈旧命中、NXE reserved-bit PF)。进 F3(信号/clone/futex/SMP)这类强并发、易死锁特性前,补齐可观测性基建——让 panic 时能看清「崩在哪 + 调用链 + 内存状态」。

用户铁律:可用可调试优先于性能(hobby os 非替代 Linux),实现对齐 Linux(CONFIG_FRAME_POINTER / KALLSYMS / panic / meminfo / pstore)。

## 范围(完成 + 推迟)

| 批 | 内容 | 状态 | commit |
|----|------|------|--------|
| 0 | frame pointer(`-fno-omit-frame-pointer`,对齐 `CONFIG_FRAME_POINTER`) | ✅ | `89029a9` |
| 1a | KALLSYMS lookup 模块(二分 `{addr,name}`)+ 单测 | ✅ | `1eaddba` |
| 2 | 防御 backtrace(RBP walk + 栈范围检查)+ 单测 | ✅ | `0fbfc43` |
| 3 | 统一 panic handler(收编 dump_registers/kpanic/fatal_halt + backtrace) | ✅ | `7904c9e` |
| 4 | `dump_memory_stats`(PMM/Slab/PageCache 汇总)+ panic 自动打 | ✅ | `114db59` |
| — | fix: backtrace `%zu`→`%u`(冒烟发现) | ✅ | `ebe2553` |
| 5 | 崩溃持久化记录 | ⏳ 推迟 | — |
| 6 | 1b 真实符号注入(nm 嵌入) | ⏳ follow-up | — |

## 关键设计点

- **frame pointer 全局开**:`add_compile_options(-fno-omit-frame-pointer)`,Release `-O2` 下 RBP 链仍完整 → backtrace 在生产内核总可用。对齐 Linux `CONFIG_FRAME_POINTER=y`。
- **KALLSYMS lookup 解耦注入源**:`kallsyms_set_table()` 注入表 + `kallsyms_lookup()` 二分(对齐 `kallsyms_lookup_name`)。生产注入(1b)就绪前显示裸地址,就绪后自动符号化——模块与注入源解耦,故 1b 可后加不阻塞 backtrace/panic。单测用 fixture 表。
- **backtrace 防御用栈范围检查(非 `translate`)**:`VMM::translate()` 不支持 huge 页(返 0,GOTCHA#13)且取 lock 有死锁风险;改用「current task 栈 / boot 栈」范围检查 + 深度上限 + 单调性(`next_rbp > cur`)。无 lock、对 huge 有效、IF=0 安全。核心 `backtrace_capture()`(纯,填地址数组,可单测)+ `backtrace_from()`(符号化打印)。
- **统一 panic**:`panic(frame, name, vec, fmt, ...)` 一站式(msg + 寄存器 + backtrace + current task + `dump_memory_stats` + halt);所有 fatal exception(#DE..#SS/#GP/#PF-fatal)+ `kpanic` 收编至此。双 sink(serial + framebuffer)由 kprintf 多 sink 天然提供。

## ⚠️ 关键教训(GOTCHA,记 PLAN OPEN GOTCHAS)

1. **`CMAKE_BUILD_TYPE` 默认空(= -O0)**:项目从未在 -O2 下验证。批0 新建 `build-rel`(`-DCMAKE_BUILD_TYPE=Release -DCINUX_BUILD_TESTS=ON`)验证 **-O2 也 752→763 全绿**(项目首次 -O2 通过,生产构建可用,无优化暴露的 UB 崩溃)。铁证:`slab.cpp.o`(-O2)函数序言有 `push %rbp × 9`。**建议 CI 加 -O2 Release 门禁**(开发仍 -O0 图快编译/gdb)。
2. **`VMM::translate()` 不支持 huge 页(返 0)**:backtrace 初版用 translate 预检,test 跑在 boot stack(很可能 huge 映射区)→ 第一帧就被拒(`g_count<3` 失败)。改栈范围检查解。教训:凡依赖 translate 判页 present 的代码,对 huge 映射区(boot stack/direct-map)失效。
3. **kprintf 不支持 `%zu`**(freestanding 子集):backtrace `(%zu frames)` 输出字面 "%zu"(冒烟触发 panic 才发现)。统一改 `%u` + `(unsigned)`cast。`%x` 是 uint64、`%u` 是 unsigned int,无 `%l`/`%z` 变体。
4. **backtrace 单测要防 tail-call**:`-O2` 对 `middle(){leaf();}` 做 tail-call(jmp,不留帧),RBP 链帧数不足;`__attribute__((noinline))` + asm barrier(`__asm__ volatile("":::"memory")`)强制留帧。

## 推迟项理由

- **M5 崩溃持久化**:前提「重启后保留」,CinuxOS 当前**无软重启/持久化层**(QEMU `isa-debug-exit` 进程退出、实机 `cli;hlt`)。冷启动/QEMU 重跑都 fresh,RAM 清零。Linux pstore 能跨重启是因有 NVRAM/保留内存 + 真实重启路径——这两样 CinuxOS 都没有。panic 的 serial 输出(`QEMU -serial file:`)已覆盖事后取证。**等有持久化层/重启流程再做。**
- **M6 1b 真实符号注入(nm 嵌入)**:本质是 CMake 两阶段链接重构(循环依赖:符号表要链接,地址来自链接;Linux 迭代链接解)。风险点(可能破坏 M7b 刚稳定的构建)。当前 backtrace 多帧**裸地址**,host `addr2line -e build/kernel/big/big_kernel <addr>` 一行降级符号化。**等从容做。**

## 验证

- run-kernel-test **763/0**(752 + KALLSYMS 6 + backtrace 4 + memstats 1)。
- **冒烟**:临时 `__asm__("ud2")` 触发 #UD → panic 端到端(`KERNEL PANIC` + 异常名 + 全寄存器 + backtrace + task + `[MEM]` 概览)。冒烟抓到 `%zu` bug 并修(`ebe2553`),验证了「触发一次才安心」的价值。
- Release(-O2)`build-rel` **763/0**(项目首次 -O2 验证)。

## 文件

- 新:`kernel/lib/kallsyms.{hpp,cpp}`、`kernel/arch/x86_64/backtrace.{hpp,cpp}`、`kernel/mm/diagnostics.{hpp,cpp}`、`kernel/test/test_{kallsyms,backtrace,memory_stats}.cpp`。
- 改:`CMakeLists.txt`(frame pointer)、`exception_handlers.cpp`(panic 收编 + backtrace + memstats)、`kprintf.cpp`(kpanic + backtrace)、各 CMakeLists/main_test(接线)。

FO 收官(核心闭环:panic 显示符号化栈结构 + 寄存器 + task + 内存概览,生产 -O2 总可用)。下个焦点 F3 信号。
