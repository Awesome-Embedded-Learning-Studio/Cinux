# F-USABILITY 批4a:g++ 单命令闭环(C++ STL + 异常)

> 2026-07-04, 分支 `feat/f-usability-b4-gpp`(从干净 main `509471d`)。目标:
> CinuxOS 上 `g++ -fno-pie -no-pie hello.cpp -o /tmp/cpp.out && /tmp/cpp.out` 跑通,
> STL(vector/string)+ 异常(throw/catch)。

## 结论

Buildroot gcc rootfs 内 `g++ hello.cpp` 链接 + 运行闭环:

```text
Hello from G++!
exception: caught
[usability] PASS gpp-compile-run
[usability] result: PASS
```

`run-buildroot-usability` gate exit 0。run-kernel-test-all 两腿 1108/0 + F-ECO bb
batch 14/14 + host 69/69,零回归。

## 修复链(7 个独立 bug)

1. **do_openat follow symlink(头号)**:`do_openat_kernel` 用 `fs->lookup`(只走
   目录组件,不 follow symlink)。open `libstdc++.so`(symlink → .so.6.0.35)返
   回 symlink inode,ld 读到 symlink 的 target 字符串当文件内容 → BFD
   "file format not recognized"。改用批1b 的 `vfs_lookup`(组件 walk + symlink
   follow + MAXSYMLINKS=40)。诊断靠 [EXT2RD]/[EXT2RL]/[LKC] 日志实证(已清)。
2. **waitpid status 编码**:`sys_exit` 存 `exit_status = code`(raw),glibc
   `WIFEXITED((status & 0xff)==0)` 对非 0 code 误判 WIFSIGNALED → `exit(1)` 被读
   成 SIGHUP,collect2 误报 "ld Hangup",g++ driver ICE(NULL deref)。抽
   `exit_and_reap_current(encoded)`:exit 路径 `(code & 0xff) << 8`(WIFEXITED),
   signal-default-kill 路径 `sig`(WIFSIGNALED 低字节,signal.cpp 改调它)。
3. **kprintf `%.*s` 精度**:诊断 readlink target 必需(返回非 NUL-term buffer)。
   `vkprintf_impl.hpp` 加 `.*`/`.N` precision(%s 用 precision 截断)。
4. **extract.sh C++ 闭包**:加 cc1plus(~50MB)+ g++ driver + libstdc++(real
   .so.6.0.X + SONAME + dev symlink)+ C++ headers 闭包(`g++ -H` 动态算)+ libm
   (glibc libm.so.6 + dev symlink;libm.so.6 文件名即 SONAME,不能 ln → self-loop)。
5. **assemble 192MB / inodes 16384**:C++ 闭包 127MB,原 128MB 装不下。+ 修
   buildroot `/usr/lib64 → /usr/lib` symlink 与 GCC closure 真目录冲突(rm 后合并)。
6. **ProcFs/DevFs lookup_child**:vfs_lookup(批1b)逐组件用 `lookup_child`,
   FileSystem 基类默认 ENOSYS。Ext2 有,ProcFs/DevFs 漏 → open `/proc/meminfo`
   ENOSYS(busybox free "Function not implemented")。两 FS 补 lookup_child(仿
   lookup 的单组件版,meminfo/<pid>/stat/cmdline)。
7. **main_test bb smoke 断言解码**:bb applet 比对改 `WEXITSTATUS` 解码(配合
   waitpid 编码改动);bb free 修 meminfo(lookup_child)后真 PASS(exit 0)。

## 验证

- `run-buildroot-usability` PASS:`Hello from G++!` + `exception: caught` +
  `PASS gpp-compile-run` + `result: PASS`(本地 build-bu + build/buildroot)。
- `run-kernel-test-all` 两腿 1108/0 + F-ECO bb batch 14/14 + ALL TESTS PASSED。
- host 单测 69/69(改公共接口 sys_exit.hpp 后全量编绿)。
- ACT gcc-smoke 本地复现:act 0.2.89 + Docker Desktop 撞 nektos/act issue 107
  (container cwd `/tmp/...` no such file + composite action path 解析空),非
  脚本/代码问题——`act_ci_job.sh` 固化 docker/image/workdir/git-init/prepare
  检查全过,真 CI 待 push。

## GOTCHA

1. **vfs_lookup 批1b 要求所有 FS 实现 lookup_child**:Ext2 有,ProcFs/DevFs 漏,
   基类返 ENOSYS。do_openat 旧 `fs->lookup`(多组件)路径不暴露(没人逐组件调),
   迁 `vfs_lookup` 才暴露。新 FS 必须 add lookup_child。
2. **libm.so.6 文件名即 SONAME**(glibc,无版本后缀):`ln -sf libm.so.6 libm.so.6`
   是 self-loop,覆盖真实文件 → ldso 死循环("cc1: cannot open libm.so.6")。只
   建 `libm.so`(dev)。libstdc++ 是 `libstdc++.so.6.0.35`(版本文件),不 self。
3. **ld stderr 经 pipe 到 collect2/g++**:waitpid 编码错时 collect2 误判 SIGHUP,
   g++ ICE,ld 真错误(stderr)被吞。修编码后 collect2 正确报 "ld returned 1",
   才看到真因(`cannot find -lm`)。
4. **`exit(code)` vs signal-kill 编码**:两者同经 sys_exit。抽
   `exit_and_reap_current(encoded)` 区分(WIFEXITED `code<<8` / WIFSIGNALED `sig`)。
   signal-default-kill(signal.cpp)改调它(传 `sig`,低字节 = WIFSIGNALED)。
5. **do_open_kernel 不 follow**(sys_open 旧路径,ring0 测试专用):生产
   sys_open/sys_openat → do_openat(已 follow)。测试 open regular 不碰 symlink,
   留 follow-up(一致性)。

## follow-up

- **ext4 rootfs**:ext2 驱动只读 ext4 extent(depth-0 leaf),extent 写是 F6-M5
  follow-up。可写 rootfs 必须留 ext2(`mkfs.ext2 -O none`)。批4a 暂扩 IMAGE_SIZE。
- **do_open_kernel follow**:测试专用 sys_open 路径不 follow(一致性,非 gate 阻塞)。
- **sys_readlink 对 regular 文件**:应返 EINVAL,现读 i_block(long symlink 路径)。
  次要。
- **PIE**:gcc/g++ 用 `-fno-pie -no-pie`(CinuxOS 只载 ET_EXEC)。PIE/ELF-base
  ASLR 独立 follow-up。
- **ACT issue 107**:nektos/act container cwd 挂载(act 0.2.89 + Docker Desktop)。
  升 act 或换 `--bind` 配置。
- **批4b pthread**:gettid/set_robust_list/sched_getaffinity 等 stub + clone
  (CLONE_THREAD) 真跑(机制有,未压测)。pthread_create/join 试金石待续。

接 [[gcc-selfhost-handoff]] / [[f-usability-symlink-follow]] / [[f-usability-ci-act-gcc-smoke]]。
