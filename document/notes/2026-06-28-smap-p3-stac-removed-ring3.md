# SMAP P3 — 撤全局 stac(SMAP 真生效)+ ring-3 真验证 + smoke 默认 ON

**日期**:2026-06-28　**分支**:feat/f10-tty-dyn
**提交**:c186879(P3 撤 stac + accessor)+ 7400261(smoke 默认 ON / CI musl cache)
**验证**:run-kernel-test-all 两 leg 960/0 + ring-3 hello 20/20(单核 + -smp2)+ forktest PASS + 无 PANIC

## 背景:P3 是 SMAP 重做的收官步
P0a-P0g 把 syscall 家族全 accessor 化。P3 撤掉全局 stac(syscall_entry + ISR),让 SMAP **真生效**(AC 默认 0,kernel 不能裸解引用 user)。这是 SMAP 重做的目标(对齐 Linux:撤全局 stac,kernel 访问 user 必走 accessor)。

## 做了什么

### 撤全局 stac + 剩余裸解引用 accessor
- 撤 syscall.S:61 + interrupts.S 3 处全局 stac(AC 默认 0)。
- cleartid(sys_exit task_exit_cleartid 写 user child_tid)+ futex_wait(读 user uaddr)+ signal_setup_frame(写 18 字段 SignalFrame 到用户栈)三个剩余裸解引用改局部 stac/clac(accessor)。
- 用局部 stac(不用 access_ok/put_user)——因为 test_clone/futex/signal 的 mock 用 kernel 地址,access_ok 会拒(同 P0f cleartid 回退教训:test 无真 user AS)。生产(musl 真 user 地址)局部 stac 正确。

### ring-3 smoke 改 20 次循环压测 + reap 内层
- main_test.cpp smoke_entry fork+execve+waitpid 改 20 次循环(压测,多次触发 accessor/cleartid/futex/signal,呼应交接 memory「-smp2 shell 多次 /hello 才偶发 SMAP #PF」)。
- smoke reap 改调 `cinux::proc::waitpid` 内层(smoke_entry 是 kernel task 无 user AS,sys_waitpid 的 put_user 拒 kernel status → -EFAULT)。

### smoke 默认 ON + CI musl cache(7400261)
- `CINUX_MUSL_HELLO_SMOKE` 默认 ON(CMakeLists)——run-kernel-test 用 kernel 地址 SMAP 不触发(假绿),只 ring-3 真验,所以必须默认 ON,否则 CI 永远假绿。
- CI kernel-tests job 加 musl sysroot `actions/cache`(ci.yml,key=build-musl.sh hash)+ build-musl/hello/forktest(cache hit 跳过 ~30s 下载/编译)。

## 关键认识:run-kernel-test 假绿(本批最重要)
撤 stac 首跑 run-kernel-test-all 仍 960/0(没崩)——看似 OK,但**假绿**:test 全用 kernel 地址,SMAP 只管 supervisor 访问 **user 页**,kernel 访问 kernel 地址不需 AC → test 路径根本触发不了 SMAP(F9 4 批 931/0 假绿同根,F-VERIFY audit 头号发现「机制没真生效」)。**只有 ring-3 musl smoke 真 user 路径才真触发。**

实证:撤 stac 首跑 ring-3 smoke 即暴露 cleartid 裸写 user clear_child_tid(CR2=0x404270)→ #PF。addr2line 确认 sys_exit → task_exit_cleartid。修(局部 stac)后全 PASS。这说明 ring-3 是验 SMAP 的**唯一真闸**。

## 验证
`CINUX_MUSL_HELLO_SMOKE=ON run-kernel-test-all` 两 leg 960/0 + 单核/-smp2 ring-3 hello 20/20 + forktest PASS + 无 PANIC。

## 关键文件
- kernel/arch/x86_64/syscall.S + interrupts.S:撤全局 stac(注释 entry stac,clac 保留)。
- kernel/syscall/sys_exit.cpp(cleartid)+ sys_futex.cpp(futex_wait)+ kernel/proc/signal.cpp(signal_setup_frame):局部 stac/clac。
- kernel/test/main_test.cpp:smoke 20 次循环 + reap 调 waitpid 内层。
- CMakeLists.txt(smoke 默认 ON)+ .github/workflows/ci.yml(musl cache + build)。

## 残留 follow-up(非本批)
- **-smp2 smoke worker 偶发 did-not-run**:scheduler pick 竞态 + fallback exit 0 假绿。详见交接 memory。
- **exception table 基建 + 负测试**:CinuxOS 无 exception table,kernel violation #PF panic 无法写负测试。需建 exception table(对齐 Linux copy_from_user -EFAULT)。
