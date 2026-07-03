# F-USABILITY: gcc-smoke CI 修复 + 本地 act 复现固化

> 2026-07-03, 分支 `feat/f-usability`。目标是修复 PR 上 `gcc-smoke`
> 失败, 并把 WSL + Docker Desktop + act 的本地复现流程固化下来。

## 结论

远程 CI 的原始失败点是 `gcc-smoke` 在 CinuxOS 内执行 `/usr/bin/gcc` 时找不到文件:

```text
[EXECVE] loading '/usr/bin/gcc'
[EXECVE] lookup failed: /usr/bin/gcc
/etc/cinux-usability-test.sh: line 44: /usr/bin/gcc: not found
[usability] FAIL gcc-compile-run
```

根因不是内核 execve 回退, 而是 Ubuntu runner 上 `/usr/bin/gcc`、`as`、`ld`
等入口可能是版本化 symlink。`tools/gcc-toolchain/extract.sh` 原来 `cp -a`
保留 symlink, 放进 rootfs 后变成 dangling symlink, CinuxOS 查路径失败。

修完后, 本地 `act gcc-smoke` 完整通过:

```text
Hello from GCC!
[usability] PASS gcc-compile-run
[usability] result: PASS
Job succeeded
```

## 远程 CI 诊断

通过 GitHub CLI 查最新失败 run:

```bash
gh run list
gh run view <run-id> --log-failed
```

当时只有 `gcc-smoke` 红, 其他 job 绿。失败发生在 `Run gcc gate`, 即
`cmake --build build --target run-buildroot-usability` 进入 QEMU 后的用户态
gcc smoke。日志显示 BusyBox 基础可用性项已过, 只在 `/usr/bin/gcc` lookup 失败。

这说明 Buildroot rootfs、kernel image、QEMU gate 本身都已跑到用户态脚本, 问题集中在
GCC closure 的 staged 文件布局。

## 修复

### 1. 解引用工具链入口

`extract.sh` 对 `/usr/bin/as`、`/usr/bin/ld`、`$GCC_BIN` driver、`cc1`、
`collect2`、`liblto_plugin.so` 等入口改用 `cp -aL` 或绝对路径复制, 让 rootfs
里存真实 ELF 文件, 不保留指向 host-only 版本化路径的 symlink。

对应提交:

```text
b53f0e8 fix(ci): 解引用 GCC 工具链入口避免断链
```

### 2. 兼容 Ubuntu GCC program/lib 分离布局

本地 Arch 上 `cc1/collect2/libgcc/crt` 基本同在 `/usr/lib/gcc/<triple>/<ver>`。
Ubuntu runner 上则可能是:

```text
/usr/libexec/gcc/x86_64-linux-gnu/13/cc1
/usr/libexec/gcc/x86_64-linux-gnu/13/collect2
/usr/lib/gcc/x86_64-linux-gnu/13/libgcc.a
/usr/lib/gcc/x86_64-linux-gnu/13/crtbegin.o
/usr/lib/x86_64-linux-gnu/crt1.o
```

因此 `extract.sh` 不再假设 `dirname(cc1)` 就是 libgcc/crt 目录, 而是用
`gcc -print-file-name=<name>` / `gcc -print-libgcc-file-name` 按 GCC 自己的 specs
复制:

- `crt1.o`, `Scrt1.o`, `crti.o`, `crtn.o`
- `crtbegin*.o`, `crtend*.o`
- `libgcc.a`, `libgcc_eh.a`, `libgcc_s.so`, `libgcc_s.so.1`
- `libc.so`, `libc_nonshared.a`, `libc.a`

同时 `cp_lib()` 会保留原绝对路径副本, 又复制一份到 `/usr/lib` 兼容 ldso 搜索。

对应提交:

```text
11b07b3 fix(ci): 兼容 Ubuntu GCC 路径并补 act 依赖
```

### 3. assemble 每次重建 GCC closure

`scripts/assemble_gcc_rootfs.sh` 原来只在 `build/gcc-root/usr/bin/gcc` 不存在时才
运行 `extract.sh`。本地 act 复跑会复用 temp build dir, 导致脚本修了但 rootfs 仍吃旧
`build/gcc-root`。这会制造“修了还像没修”的假失败。

现在 assemble 每次都重建 GCC closure。这个步骤很快, Buildroot 大头仍由
`build/buildroot-ci` 缓存。

## 本地 act 复现

### 关键环境

宿主 WSL shell 可以直接访问用户代理:

```bash
curl -I --max-time 5 -x http://127.0.0.1:7890 http://archive.ubuntu.com/ubuntu/
```

但 act 的 Ubuntu 容器内 `127.0.0.1` 是容器自己, 不是 Windows/WSL 宿主。容器内要用:

```text
http://host.docker.internal:7890
```

实测容器内经该代理访问 Ubuntu archive 返回 200。

### act 踩坑

直接在真实 repo 上跑 `act pull_request -j gcc-smoke` 不安全。`actions/checkout`
会尝试清空工作区, 本轮曾出现:

```text
Deleting the contents of '/home/charliechen/CinuxOS'
```

虽然及时中止且主 worktree 未损坏, 但结论明确: 本地 act 不应直接 checkout 覆盖真实仓库。

此外 `catthehacker/ubuntu:act-latest` 比 GitHub hosted runner 精简, 缺过:

- `cmake`
- `bc`
- `xxd`

这些已加入 `gcc-smoke` apt 包列表。GitHub runner 已有时会打印 `already the newest version`,
安全跳过; act 精简镜像没有时会安装。

### 固化脚本

新增:

```bash
scripts/act_ci_job.sh [job]
```

默认 job 是 `gcc-smoke`。脚本会:

1. `rsync` 当前工作树到 `/tmp/cinux-act-<job>`。
2. 排除 `.git` 和主仓库 `build/`, 但保留 temp workdir 自己的 `build/` 作为本地缓存。
3. 在 temp workflow/action 中移除 `actions/checkout` 步骤, 避免误伤真实 worktree。
4. 对 composite action 的 apt install 加 `sudo -E apt-get update/install`, 让代理环境传给 apt。
5. 默认注入 `HTTP_PROXY/HTTPS_PROXY=http://host.docker.internal:7890`。
6. 默认 `--pull=false --action-offline-mode`, 复用本机已有 act action/image 缓存。

常用命令:

```bash
scripts/act_ci_job.sh
scripts/act_ci_job.sh gcc-smoke
ACT_OFFLINE=0 scripts/act_ci_job.sh gcc-smoke
ACT_PROXY= scripts/act_ci_job.sh gcc-smoke
```

固化提交:

```text
0022cd7 chore(ci): 固化本地 act 复现入口
```

并在 `AGENTS.md` 登记: 后续本地复现 GitHub Actions/CI 优先使用
`scripts/act_ci_job.sh [job]`。

## 验证

### 工具链抽取快速检查

```bash
bash -n tools/gcc-toolchain/extract.sh scripts/assemble_gcc_rootfs.sh
GCC_BIN=gcc tools/gcc-toolchain/extract.sh /tmp/cinux-gcc-root-check2
```

本地 Arch 抽取仍正常, 约 71 MB。

### act gcc-smoke

在 `/tmp/cinux-act-gcc-smoke` 内, 经固化流程等价运行:

```bash
act push -j gcc-smoke \
  -P ubuntu-latest=catthehacker/ubuntu:act-latest \
  --pull=false --action-offline-mode \
  --cache-server-addr 127.0.0.1 \
  --artifact-server-addr 127.0.0.1 \
  --bind --reuse \
  --env HTTP_PROXY=http://host.docker.internal:7890 \
  --env HTTPS_PROXY=http://host.docker.internal:7890 \
  --env http_proxy=http://host.docker.internal:7890 \
  --env https_proxy=http://host.docker.internal:7890 \
  --env NO_PROXY=localhost,127.0.0.1,::1 \
  --env no_proxy=localhost,127.0.0.1,::1
```

最终:

```text
Hello from GCC!
[usability] PASS gcc-compile-run
[usability] result: PASS
[100%] Built target run-buildroot-usability
Job succeeded
```

### 仓库验证

`cmake --build build --target run-kernel-test -j$(nproc)` 退出 0, 但本机 QEMU 仍报:

```text
qemu-system-x86_64: -accel kvm: Could not access KVM kernel module: Permission denied
```

因此该命令没有产出真实 kernel test 汇总。本批对 CI 失败面的有效验证是 act
`gcc-smoke` 完整通过。

## GOTCHA

1. **容器代理地址不是 `127.0.0.1`**: WSL/宿主 shell 用 `127.0.0.1:7890`, Docker
   容器里用 `host.docker.internal:7890`。
2. **不要让 act checkout 真实 worktree**: `actions/checkout` 可能清目录。本地复现应在
   `/tmp/cinux-act-*` temp workdir, 并移除 checkout。
3. **act cache warning 不等于失败**: `actions/cache` 可能报 `ECONNREFUSED 127.0.0.1:<port>`,
   本轮不致命, 只是 restore/save cache miss。真正的 Buildroot 增量来自 temp `build/` 目录。
4. **GCC program dir 和 lib dir 可分离**: Ubuntu 的 `cc1/collect2` 在 `/usr/libexec`,
   `libgcc/crt` 在 `/usr/lib/gcc` 和 multiarch libdir。不要用 `dirname(cc1)` 推断全部路径。
5. **生成物陈旧会制造假失败**: `build/gcc-root` 不是可靠 cache, `assemble_gcc_rootfs.sh`
   每次重建 closure 更稳。

## follow-up

- `scripts/act_ci_job.sh` 目前优先为 `gcc-smoke` 打磨; 其他 job 若要本地跑, 可能还要补
  act 镜像缺失的 apt 包。
- 若后续需要上传 artifact/cache 在 act 中完全工作, 可再研究 act cache/artifact server
  绑定问题; 当前 CI 复现不依赖它。
- glibc probe 的 `rseq/getcpu/prlimit64` 等 `ENOSYS` 降噪仍是独立内核 follow-up。
