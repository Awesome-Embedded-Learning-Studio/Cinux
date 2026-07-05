# 2026-07-05 补 gcc/g++ 编译缺漏的 8 个 syscall

## 背景

PIE 弧收官后(批1 kernel + 批2 toolchain),`run-buildroot-usability` 跑 gcc/g++ 编译闭环,但 kernel 日志 spam `unhandled syscall N`(90 次/轮)。memory [[gcc-missing-syscalls]] 登记"glibc probe getcpu/clone3/rseq + g++ 更多,先 dump top-N"。本批 dump + 补全。

## 收集(一轮编译)

`run-buildroot-usability`(gcc+g++ 完整编译流程:cc1/cc1plus→as→ld + 跑 a.out/cpp.out)日志 grep `unhandled syscall`:

| N  | name            | 次数 | glibc/musl 用途              |
|----|-----------------|------|------------------------------|
| 302| prlimit64       | 32   | malloc 探 RLIMIT_AS          |
| 318| getrandom       | 21   | canary / ASLR 随机源         |
| 334| rseq            | 12   | restartable sequence 探测    |
| 273| set_robust_list | 12   | robust futex 探测            |
| 435| clone3          | 8    | fork 替代探测                |
| 96 | gettimeofday    | 2    | 时间                         |
| 201| time            | 2    | 时间                         |
| 40 | sendfile        | 1    | 文件传输                     |

共 90 次。都不致命(glibc/musl 探测后 fallback `-ENOSYS`),但日志噪声 + 缺真功能(canary / 真时间)。

## 改动(8 syscall,4 文件对)

| syscall          | 实现   | 行为                                                       |
|------------------|--------|------------------------------------------------------------|
| rseq(334)        | stub   | -ENOSYS(glibc 放弃 rseq 路径)                            |
| clone3(435)      | stub   | -ENOSYS(glibc fallback clone/fork)                       |
| set_robust_list(273) | stub | 0(robust-futex 探测满足;真清理留 pthread 批)            |
| sendfile(40)     | stub   | -ENOSYS(cp/copy fallback read+write)                     |
| prlimit64(302)   | stub   | 0 + rlimit{RLIM_INFINITY}(无强制 limit,brk/mmap 自由长) |
| getrandom(318)   | 实现   | g_random.fill + copy_to_user 分块(256 B chunk)           |
| gettimeofday(96) | 实现   | CLOCK_REALTIME → timeval{tv_sec, tv_usec}                |
| time(201)        | 实现   | CLOCK_REALTIME → time_t 秒                                |

文件:`sys_linux_stubs.{hpp,cpp}`(4 stub)+ `sys_prlimit64.{hpp,cpp}` + `sys_getrandom.{hpp,cpp}` + `sys_time.{hpp,cpp}`(gettimeofday + time)+ `syscall_nums.hpp` 加 8 enum + `arch/x86_64/syscall.cpp` include/register + `kernel/syscall/CMakeLists.txt` 加 4 .cpp(**手动列源,非 GLOB**——加文件必须改 CMakeLists)。

复用:`gettimeofday`/`time` 走 `do_clock_gettime_kernel(CLOCK_REALTIME)`(F5-M4 HPET + RTC);`getrandom` 走 KRandom(F9 batch 7 xoshiro256**)。

## 验证

- `run-buildroot-usability`:`unhandled syscall` **90 → 0**;`[usability] result: PASS`(gcc-compile-run + gpp-compile-run PASS,Hello from GCC/G++!)。
- `run-kernel-test-all` 两 leg:hello 20/20 + hello-dyn(PIE)5/5 PASS(回归无破坏)。

## follow-up

- **199 fremovexattr**:run-kernel-test-all test 路径暴露(2 次;gcc/g++ 编译没见)。xattr 系列未实现,低频,留后续。
- **robust futex 真清理**:`set_robust_list` 现返 0 假成功(pthread 死时清 robust lock 未实现),pthread 批再补。
- **sendfile 真实现**:cp/copy 大文件优化时再补(现 fallback read+write,功能正常)。

接 [[gcc-missing-syscalls]]。
