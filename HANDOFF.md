# HANDOFF: Windows 支持第一层（运行层）+ 编译层甲（WSL2）

分支 `feat/windows-support`，基于 `main` 的 `78c9273`。没有 push。

## 做了什么

- `cmake/qemu/devices.cmake`
  - 新增缓存选项 `CINUX_QEMU_ACCEL`，取值 `auto`（默认）/`kvm`/`whpx`/`hvf`/`tcg`。
  - configure 时跑一次 `qemu-system-x86_64 -accel help` 看 QEMU 支持哪些加速器。KVM 额外检查 `/dev/kvm` 是否存在且可读写，因为 `-accel help` 会列出没有设备节点的 kvm。
  - `auto` 按平台顺序挑：Linux `kvm` → `tcg`，Windows `whpx` → `tcg`，macOS `hvf` → `tcg`。显式指定但不可用时回退 `tcg` 并打 warning。
  - CPU 跟着变：`kvm`/`hvf` 用 `-cpu host`，`whpx`/`tcg` 用 `-cpu max`。
  - 回退到 TCG 时打印一行提示（emulated, slow）。配置结果为配置时快照，升级 QEMU 后要重新 configure。
- `cmake/test/run_kernel_tests.cmake`
  - 删掉「没有 `/dev/kvm` 就 `FATAL_ERROR`」。改为读取 `QEMU_DEVICE_ARGS` 里的 `-accel`，如果是 `kvm` 而 `/dev/kvm` 不可用，就把参数改成 `-accel tcg -cpu max`，打印 warning，并继续跑。
  - TCG 下单个测试的超时从 60 秒放宽到 100 秒（CTest 外层仍是 120 秒）。CI 有 KVM，参数保持原样。
- `scripts/windows/run-windows.ps1`
  - 找 `qemu-system-x86_64`（PATH，再试 `Program Files\qemu`）。
  - 用一个 `-machine none -S` 的空机器探测 WHPX：1.5 秒内退出说明初始化失败，进程还活着说明可用。可用就选 WHPX，否则打印提示退 TCG。
  - `-Kernel`、`-Disk`、`-Memory`、`-Cpus`、`-Accel` 参数，默认从 `<仓库>\build\boot` 取 `cinux.img` 和 `ext2.img`。
  - 参数拼装和 Linux `run` 目标一致：AHCI + ext2 盘、USB 键鼠、e1000、`isa-debug-exit`，`-display none`，`-serial stdio`。中文注释和 comment-based help。
- `scripts/windows/setup-wsl2.sh`
  - WSL 检查、Ubuntu 检查、apt 依赖安装（清单对齐 `.github/actions/setup-cinux`）、`gcc-14` 选择、CMake configure、构建 `test_host`。
  - 检查 `/dev/kvm`，没有就说明会退 TCG，并给出 `.wslconfig` 嵌套虚拟化的写法。
  - 结束时打印生成镜像、跑冒烟测试、启动系统的下一步命令。
- `document/ci/08-windows.md`
  - 运行层步骤：镜像从哪来、装 QEMU、启用 WHPX、跑脚本、参数表。
  - 编译层甲：WSL2 安装、克隆、一键脚本、生成镜像、跑测试、启动、把镜像交给 Windows。
  - CMake 加速器选项说明、限制与已知问题、排错表。

## 怎么验的

本机是 Linux，QEMU 11.1.2，没有 `/dev/kvm`，没有 `pwsh`。

- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` 通过，输出：
  `-- QEMU acceleration: no hardware accelerator usable on Linux, falling back to tcg (emulated, slow)`。
- `cmake --build build --target test_pmm -j6` 通过。
- `bash -n scripts/windows/setup-wsl2.sh` 通过。
- `python3 -m unittest discover -s test/tools -p 'test_ci_checks.py'` 17 个测试通过。这条单测会用不带 `-accel` 的合成参数调 `run_kernel_tests.cmake`，所以新逻辑在没有 `-accel` 时保持原样不动。
- `run-windows.ps1` 没有 PowerShell 可执行，**没有真跑**。做的是：一个能识别 PowerShell 单引号/双引号/行注释/块注释/here-string 的括号与引号平衡检查（通过），以及逐行人工检查。WHPX 分支的可执行验证要等 Windows 机器。
- TCG 冒烟测试：构建 `boot_ktest_console`，跑
  `ctest --test-dir build/test -R '^kernel_test_console_cpu1$' --output-on-failure`，通过，serial 里是 `=== Results: 1 passed, 0 failed ===`。
- 回退路径单独测了：把 `-accel kvm -cpu host` 手工传给 `run_kernel_tests.cmake`（这台机器没有 `/dev/kvm`），脚本打印
  `/dev/kvm is not usable here: falling back to tcg ...`，改参数后测试通过。
- 显式选项：`-DCINUX_QEMU_ACCEL=kvm` 触发 warning 并回退 tcg；`-DCINUX_QEMU_ACCEL=bogus` 触发 `FATAL_ERROR`。
- 生产镜像 TCG 真跑：构建 `boot_image` 后，用 `qemu-device-args.txt` 里的 tcg 参数加 `build/boot/cinux.img`、`ext2.img` 启动，串口日志走到 `/ #` 提示符（`Cinux up`、`vesper-fbdev` 正常），45 秒 timeout 时进程仍在跑，属正常。
- 本机沙箱的 `/var/tmp` 只读，QEMU `-snapshot` 会报 `Could not open temporary file`。设 `TMPDIR=$PWD/build/tmp` 后正常。这是本机环境限制，仓库里没改这个。

## 限制

- WHPX 路径没在真实 Windows 上跑过。QEMU 探测逻辑本身在 Linux 上用 KVM/TCG 验证过（KVM 缺设备立刻退出，TCG 正常起机器），但 Windows 上没有实测。
- `run-windows.ps1` 只支持串口控制台，没有 GUI。`run_gui` 依赖 GTK，Windows 原生 QEMU 不在这条路径上。
- TCG 慢。生产镜像在这台机器上大约几十秒到 shell，全套内核测试会明显更久。
- 当前 GitHub Release 只有源码归档，没有 `cinux.img`/`ext2.img`。运行层的镜像实际要先走编译层甲构建。文档里如实写了。
- `setup-wsl2.sh` 只做了 `bash -n`，没有在 WSL 里真跑（本机不是 WSL，装系统包需要权限）。apt 包名按 Ubuntu 24.04 + CI 清单写的，换发行版需要自己核对。
- 构建 `test_host` 需要先 `git submodule update --init --recursive`。本次为了跑 TCG 内核测试，在 worktree 里初始化了 `third_party/vesper` 和它的 `third_party/visor` 子模块，没有改 submodule 指针。

## 剩什么

- 在真实 Windows 11 机器上验证 WHPX：`run-windows.ps1` 默认路径、`-Accel whpx`、WSL2 嵌套虚拟化下的 `/dev/kvm`。
- 让 Release 带上 `build/boot/cinux.img` 和 `ext2.img`（或一个打包脚本），运行层才能真的「下载即跑」。
- 编译层乙：MSVC / clang-cl / MinGW 原生 Windows 工具链，远期。
- Windows 图形显示路径（GTK 或换显示后端），目前只保证串口。
- `scripts/windows/setup-wsl2.sh` 在 WSL2 上的端到端实跑。
