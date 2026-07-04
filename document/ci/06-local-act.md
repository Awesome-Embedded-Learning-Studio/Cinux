# Local CI with act / 本地 CI 复现手册

本文记录 Cinux 在 WSL + Docker Desktop 下用 `act` 复现 GitHub Actions 的固定入口。不要直接在真实 worktree 里跑 `act`：`actions/checkout` 会清理工作区，容易误伤未提交改动。

## 快速入口

```bash
scripts/act_ci_job.sh gcc-smoke
```

脚本默认做这些事：

- 检查 `rsync`、`git`、`python3`、`act`、`docker` 是否可用。
- 检查 Docker daemon 是否能从当前 WSL shell 访问。
- 离线模式下检查 `catthehacker/ubuntu:act-latest` 镜像是否已经在本机。
- 默认检查宿主代理 `http://127.0.0.1:7890`，容器内使用 `http://host.docker.internal:7890`。
- 同步当前 worktree 到 `build/act-<job>`，只 patch 临时副本。
- 临时副本会在 `actions/checkout` 之后插入 apt proxy 配置和 `apt-get update`，避免 act 镜像缺 apt index 或 `sudo apt-get install` 不继承代理时卡住。
- 临时副本会把 `actions/upload-artifact` 替换成本地 log fallback，避免 act artifact API 错误盖住真正失败。
- 生成 pull_request event JSON，初始化临时 git repo。
- 用 `act -C build/act-<job>` 运行，避开 act 0.2.89 在 Docker Desktop/WSL 下把 host cwd 当 container cwd 的问题。

## 常用命令

```bash
# 只准备临时副本，不跑 act
ACT_PREPARE_ONLY=1 scripts/act_ci_job.sh gcc-smoke

# 第一次本机 action cache 为空时允许 act 联网拉 actions
ACT_OFFLINE=0 scripts/act_ci_job.sh gcc-smoke

# 关闭代理
ACT_PROXY= scripts/act_ci_job.sh gcc-smoke

# 指定临时目录
ACT_WORKDIR=/tmp/cinux-act-gcc-smoke-try1 scripts/act_ci_job.sh gcc-smoke
```

若镜像不存在，先拉镜像：

```bash
docker pull catthehacker/ubuntu:act-latest
```

## 远程 CI 排查

先看 GitHub Actions 权威结果：

```bash
gh run list --repo Awesome-Embedded-Learning-Studio/Cinux --limit 10
gh run view <run-id> --job <job-id> --log
```

`gcc-smoke` 失败时优先看 `Run gcc gate` 段。2026-07-04 的 PR #64 远程失败表现是：

- job commit: `9cefe7dc129543434790f7e0edfce93e64cfee82`
- step: `Run gcc gate`
- command: `timeout 120 cmake --build build --target run-buildroot-usability`
- remote result: QEMU 被 timeout 发送 SIGTERM，exit code `124`

本地 act 通过新脚本可跑到业务失败点，而不是 act 自身失败点。2026-07-04 的干净验证命令：

```bash
timeout 900 scripts/act_ci_job.sh gcc-smoke
```

该次本地复现结果：

- `Run gcc gate` 进入 QEMU / Buildroot 测试。
- kernel 在 `kernel_init` 早期触发 `Double Fault`。
- QEMU 通过 `isa-debug-exit` 返回 `21`，`run-buildroot-usability` 失败。
- 本地 artifact fallback 成功打印 `build/gcc-smoke.log` 尾部，没有再被 `CreateArtifact: ECONNRESET` 之类的 act artifact 问题遮住。

远程和本地现象可能因为 timeout、当前分支 HEAD、缓存状态略有差异；以 GitHub Actions 为最终权威，但本地 act 现在能稳定区分“环境/act 管道失败”和“CI 业务失败”。

## 已知 act 坑

act 0.2.89 在这台 WSL + Docker Desktop 环境下，直接 `cd /tmp/cinux-act-... && act ... --bind` 会失败：

```text
failed to read 'action.yml' from action './.github/actions/setup-cinux' with path ''
chdir to cwd "/tmp/cinux-act-..." no such file or directory
```

根因是 act 把 host workdir 路径当 container cwd，但实际 mount 到 container 内其他路径。固定绕法是脚本现在使用的 `act -C <repo-local-temp-worktree>`。

## 缓存说明

脚本会设置本地 cache/artifact server，并把 cache 放在 `build/act-<job>/.act/cache`。act 0.2.89 需要保留 job 里的 `actions/checkout` 才能解析本地 composite action，所以本地副本可能被 checkout 清理；Buildroot 首轮仍可能是冷编。

如果 cache server 报端口冲突，可换端口：

```bash
ACT_CACHE_PORT=34569 ACT_ARTIFACT_PORT=34570 scripts/act_ci_job.sh gcc-smoke
```

如果 action cache 为空且 `ACT_OFFLINE=1`，act 可能找不到 `actions/checkout`、`actions/cache` 或 `actions/upload-artifact`。第一次可以用 `ACT_OFFLINE=0` 预热。
