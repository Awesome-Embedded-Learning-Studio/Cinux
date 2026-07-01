# F-ECO 批5:sysinfo + getrusage

> 2026-07-01。外包 worktree `feat/outsource-f-eco-b5`(从集成线 `e2a7d70`),cherry-pick 回 `feat/f-eco-b2-vfs-syscalls`(`b4fa398`)零冲突。两 leg **1051/0**(1047+4)+ host 69/69。**未 push**。
> busybox 试金石第五刀的**内核件**:`ps`/`free`/`uptime`/`top`/`time`。`ps` 走已有 ProcFS(F6-M2 readdir + /proc/<pid>/stat);`free`/`uptime` 用 sysinfo;`time`/`top` 用 getrusage。busybox applet 端到端验收留 CI build。

## sysinfo(99)

- `sys_sysinfo` / `do_sysinfo_kernel`([sys_sysinfo.cpp](../../kernel/syscall/sys_sysinfo.cpp)):填 Linux `struct sysinfo`(112B,镜像 musl/glibc 布局,`static_assert(sizeof==112)`)。
  - `uptime` ← HPET monotonic 秒(复用 clock_gettime/nanosleep 的 HPET+PIT 兜底 helper)。
  - `totalram`/`freeram` ← `g_pmm.total_page_count()`/`free_page_count()` × 4096。
  - `procs` ← 沿 `signal_nth_task_pid(n,&pid)` 计数活 task(F6-M2 的 ProcFS accessor,PID_MAX 小有界)。
  - `memunit=1`(RAM 字段是字节)。
  - **loads/sharedram/bufferram/totalswap/freeswap/totalhigh/freehigh=0**——CinuxOS 未跟踪(load avg/buffer/swap/highmem),**诚实非编造**;真值留会计 + swap 里程碑。
- `do_` 内核变体供 ring0 测试直调(`sys_sysinfo` 是 user 边界 copy_to_user,测试内核栈地址过不了 is_user_vaddr,见 [[f-eco-b3-b4-nanosleep-dup-fcntl]] GOTCHA 2)。

## getrusage(98)

- `sys_getrusage` / `do_getrusage_kernel`:填 Linux `struct rusage`(144B,`static_assert(sizeof==144)`),**全 0**(无 per-task 会计:user/sys CPU、maxrss、fault、ctxsw 都不跟踪)。
- `who` 校验 {-1,0,1}(RUSAGE_SELF/CHILDREN/THREAD——内核 SELF=1/CHILDREN=-1/THREAD=1 与历史 libc SELF=0 约定冲突,取**并集**不拒合法 caller);未知→EINVAL。
- 诚实占位:busybox `time`/`top` 链接上但读 0,待会计里程碑填真值(只填字段,不动 syscall 边界)。

## 机制测试(防假绿,4 测)

- sysinfo sane:`totalram>0`、`freeram>0`、`freeram<=totalram`、`uptime>=0`、`procs>0`、`memunit=1`、未跟踪字段=0、**cross-check `totalram == g_pmm.total_page_count()*4096`**(证 PMM 接对了,非编造数字)。
- sysinfo bad-ptr→EINVAL;getrusage zeros(poison `ru_maxrss=99` 验清零);getrusage bad-who(99)→EINVAL。

## follow-up(留后续)

- **busybox applet 端到端验收**(ps/free/uptime/top/time 真跑):留 CI build busybox -O2(本地无 build)。
- **load average**(sysinfo.loads):需调度器采样Runnable 任务(周期计算)。
- **swap**(sysinfo.totalswap/freeswap):需 swap 子系统。
- **per-task 资源会计**(getrusage 真值):user/sys CPU 时间、maxrss、fault、ctxsw 计数。
- **/proc/meminfo + /proc/uptime**(busybox `free`/`uptime` 备选路径,现走 sysinfo 够用)。

## 验证

`run-kernel-test-all` 两 leg:单核 1051/0 → -smp 2 1051/0 → ALL TESTS PASSED(基线 1047 + 4 sysinfo/getrusage)。`test_host` 69/69(只加 syscall 枚举 + 新 handler,公共接口零改)。

**push/PR 归用户**——F-ECO 外包线,等回主线。
