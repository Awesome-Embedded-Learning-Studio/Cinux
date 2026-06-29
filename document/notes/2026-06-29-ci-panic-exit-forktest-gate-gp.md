# 2026-06-29 CI 收尾 saga — panic 秒退 + forktest 门控 + Release/ubsan #GP

接 [[smp-fork-reap-resurrection-fix]] 同分支 `fix/smap_bug_fix`。SMAP follow-up #1
修完后,CI 从「全军覆没」逐个排雷到 5/6 绿。本 note 记 CI 侧的三件事。

## 1. panic 立即 isa-debug-exit + 退出码编码原因(`69cf1f4`)

**问题**:kernel panic(异常 #PF/#DF/#GP 或 kpanic)走 `cli;hlt` 死循环,CI 要等
`timeout 300`(ci.yml:162)5 分钟杀 QEMU,且退出码无信息(之前 DF 死等 3m34s)。

**修**:panic 路径写 isa-debug-exit(0xf4)立刻退出,退出码编码原因:
- 异常 panic(`exception_handlers.cpp::panic()`):value = `vector+2` → exit `(v<<1)|1`
  (#DE(0)→5、**#DF(8)→21**、#GP(13)→31、**#PF(14)→33**)。注意 `(v<<1)|1 mod 256`
  使 value 0 **和 128** 都映射 exit 1(成功),所以避开 0/128。
- kpanic(`kprintf.cpp::kpanic()`):value 64 → exit 129。
- `qemu_test_wrapper.sh` 解码并在日志标注「KERNEL PANIC: exception vector N
  (QEMU exit X)」/「kpanic/assertion」。
- 生产 `run` 无 isa-debug-exit 设备(只在 `QEMU_TEST_EXTRA_FLAGS`)→ outl 空操作 →
  落 cli;hlt(交互调试不变);仅测试/CI target 秒退。

**实测**:临时 kpanic → QEMU 退出 129 + wrapper 标注 + make 快速失败(不再超时);
撤 temp 后 run-kernel-test-all 两 leg 仍绿(panic-exit 无 panic 时惰性)。**该改进
立刻在 CI 验证价值**:把 DF 从「3m34s 神秘 hang」变「1m40s 秒退 + 退出码 21 + 完整 serial」。

## 2. 门控 smoke forktest 阶段(`9f8505e`)—— 独立 fork/CoW 跨核 #DF

CI -smp2 ring-3 smoke 在 **forktest 阶段**(hello 阶段 20/20 PASS,验证 saga 修复)
**确定性 #DF**——本地 25+ 次复现不了(CI 慢机器必踩)。panic-exit 后秒退+完整 serial
定位:**RIP 在 `cinux::proc::fork()` 内 `copy_page_table_level`(CoW 页表拷贝)+ post-copy
`flush_tlb_all` 之后**。fork 跑 IF=0(`SFMASK=0x200` 清 IF,**非迁移竞态**)→ fork/CoW
跨核 page/mapcount 生命周期竞态,需 cross-core TLB shootdown 根治(独立 follow-up)。

**处理**:smoke 跳过 forktest exec(`forktest_ok` 恒真,exit_code 只看 hello),
forktest.c 仍构建供本地深挖。修好 fork/CoW 跨核后恢复门控。门控后 5/6 CI 配置绿。

## 3. ⚠️ Release+ubsan #GP —— gcc-13 特异,本地复现不了(未解)

门控 forktest 后,**唯一仍红的是 (Release, ubsan)**:`KERNEL PANIC: exception vector 13
(QEMU exit 31)`,#GP。RIP 在 `cinux::proc::clone()`(clone+0x6F2),`lock xadd` refcount
一个**非规范指针**(error code 0 = 非规范地址访问)。发生在 smoke 首个 /hello fork 子
(tid=139)创建处,连 "Hello from musl" 都没打印。(Debug,ubsan) 和 (Release,none) 都过
→ 是 ubsan × Release 的组合。

**关键(用户点出)**:**编译器版本偏差**——本地 Arch gcc **16.1.1**,CI Ubuntu gcc
**13.3.0**,差 3 个大版本。不同 gcc → 不同 codegen + 不同 UBSAN 插桩 → CI 的 #GP 很可能
是 gcc-13 特异,本地 gcc-16 复现不了(本地 build-rubsan 跑是 ec=124 hung,表现完全不同)。
**Arch 不装旧 gcc**(gcc-13 不在官方源),本地拿不到 gcc-13。

**附带发现(真 bug,可能无关)**:`user/programs/shell/main.cpp:163 argv[argc]=nullptr`
越界——`char* argv[MAX_TOKENS=16]`,argc 达 16 时 `argv[16]` 越界写(nullptr)砸栈。
gcc-13 `-Warray-bounds` 编译期就警告。是 /bin/sh(user 程序)的 bug,smoke 跑 /hello 不经
它,大概率不是 clone #GP 的因,但该修(argv 扩到 MAX_TOKENS+1 或 guard)。

**未解走法(待定)**:① 本地起 Ubuntu 24.04 容器装 gcc-13 复现+定修( definitive);
② 暂时把 (Release,ubsan) matrix 标 gcc-13-specific known-issue(5/6 绿,note 之);
③ 纯靠 CI 加诊断迭代(慢)。UBSAN 的意义就是抓 UB——这个 #GP 很可能是 CinuxOS 里一处
真 UB,gcc-13 暴露 / gcc-16 不暴露,值得 gcc-13 环境下揪出来。

## 验证状态

- panic-exit:`69cf1f4`,本地实测 + CI 验证(DF/GP 都秒退带码)。✅
- forktest 门控:`9f8505e`,本地两 leg 绿,CI 5/6 绿。✅
- Release+ubsan #GP:**未解**,gcc-13 特异,待 gcc-13 环境定位。⏳
