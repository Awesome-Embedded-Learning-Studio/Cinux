# Windows 支持 / Windows Support

Cinux 在 Windows 上有两条路。运行层直接拿构建好的镜像，在 Windows 原生 QEMU 里跑。编译层甲在 WSL2 里从源码配置、构建、跑测试。两条路都不改内核，改的只是 QEMU 把哪台机器、用哪种加速器交给内核。

| 层 | 在哪跑 | 现在能做什么 | 状态 |
|----|--------|--------------|------|
| 运行层 | Windows 原生 QEMU | 用 WHPX 或 TCG 启动 `cinux.img`，串口进控制台 | 已实现，WHPX 未在真 Windows 机器上验证 |
| 编译层甲 | WSL2 + Ubuntu 24.04 | 装依赖、CMake 配置、构建、跑主机与内核测试、启动系统 | 已实现 |
| 编译层乙 | Windows 原生工具链（MSVC / clang-cl / MinGW） | 无 | 远期，不在本次范围 |

## 运行层：Windows 原生跑镜像

### 1. 准备镜像

运行层需要 `build\boot` 下的两个文件：

| 文件 | 用途 |
|------|------|
| `cinux.img` | 引导镜像，MBR、stage2 和内核都在里面 |
| `ext2.img` | ext2 根文件系统盘 |

当前 GitHub Release 只有源码归档，没有附带编译好的镜像。所以第一次使用要先按下面的「编译层甲」在 WSL2 里构建一次，再把这两个文件复制到 Windows 目录。如果之后某个版本开始附带镜像，解压后直接照本节继续。

镜像路径不限制。脚本默认从仓库的 `build\boot` 找，也可以用 `-Kernel`、`-Disk` 指到任意位置。

### 2. 安装 QEMU

从 <https://www.qemu.org/download/#windows> 下载 Windows 安装器，安装时选上 “Add QEMU to PATH”，或者记住安装目录（默认 `C:\Program Files\qemu`）。

要求 QEMU 8.0 以上。太老的版本可能没有 WHPX 加速器。装完验证：

```powershell
qemu-system-x86_64 --version
```

脚本会先在 PATH 里找 `qemu-system-x86_64`，找不到再试 `C:\Program Files\qemu` 和 `C:\Program Files (x86)\qemu`。

### 3. 启用 WHPX（可选，但强烈建议）

WHPX 是 QEMU 在 Windows 上的硬件加速接口。不开也能跑，只是会退到 TCG 软件模拟，慢很多。

1. 在 BIOS/UEFI 里开启 Intel VT-x 或 AMD-V。
2. Windows 11 打开「设置 → 系统 → 可选功能 → 更多 Windows 功能」，勾选「Windows 虚拟机监控程序平台」和「虚拟机平台」，重启。
3. 验证：

```powershell
qemu-system-x86_64 -accel help
```

输出里要有 `whpx`。没有这一行说明 QEMU 构建太旧，或者 Windows 功能没启用。

### 4. 启动

在仓库根目录或任意位置执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\run-windows.ps1
```

参数：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `-Kernel` | `<仓库>\build\boot\cinux.img` | 引导镜像 |
| `-Disk` | `<仓库>\build\boot\ext2.img` | ext2 根盘 |
| `-Memory` | `1024` | 内存，单位 MiB |
| `-Cpus` | `2` | SMP 核数 |
| `-Accel` | `auto` | `auto` 先试 WHPX 再退 TCG。`whpx` 强制 WHPX。`tcg` 强制软件模拟 |

例子：

```powershell
# 镜像在别处
powershell -ExecutionPolicy Bypass -File scripts\windows\run-windows.ps1 `
    -Kernel D:\cinux\cinux.img -Disk D:\cinux\ext2.img

# 机器不支持 WHPX，直接走软件模拟
powershell -ExecutionPolicy Bypass -File scripts\windows\run-windows.ps1 -Accel tcg
```

启动前脚本会用一个空机器探测加速器：能正常初始化就选 WHPX，初始化失败就打印一行提示并改用 TCG。串口直接接在当前控制台，日志滚完会进系统，按 `Ctrl+C` 退出。

脚本拼的参数和 Linux 上的 `run` 目标一致：AHCI 上的 ext2 盘、USB 键鼠、e1000 网卡、`isa-debug-exit`，显示器关闭，只用串口。

## 编译层甲：WSL2 从克隆到启动

### 1. 装 WSL2 和 Ubuntu 24.04

在管理员 PowerShell 里：

```powershell
wsl --install -d Ubuntu-24.04
wsl -l -v
```

`VERSION` 必须是 `2`。Cinux 需要 GCC 14，Ubuntu 22.04 的官方源只有 GCC 13，不要用旧发行版。

### 2. 克隆仓库

在 WSL 的 shell 里（不是 `/mnt/c`，放在 WSL 自己的 ext4 文件系统里构建快很多）：

```bash
git clone --recursive https://github.com/Awesome-Embedded-Learning-Studio/Cinux.git
cd Cinux
```

### 3. 一键装环境

```bash
bash scripts/windows/setup-wsl2.sh
```

脚本做四件事：

1. `apt-get update`，安装 `gcc-14`、`g++-14`、`cmake`、`ninja-build`、`qemu-system-x86`、`e2fsprogs`、`libssl-dev` 等依赖，清单和 CI 的 `.github/actions/setup-cinux` 一致。
2. 用 `gcc-14` / `g++-14` 配置 `build` 目录，Debug + Ninja。
3. 构建 `test_host` 目标。
4. 检查 `/dev/kvm`，告诉你内核测试会走 KVM 还是 TCG。

构建目录和并行度可以覆盖：

```bash
CINUX_BUILD_DIR=build-win CINUX_JOBS=8 bash scripts/windows/setup-wsl2.sh
```

### 4. 生成镜像

```bash
cmake --build build --target boot_image ext2_img -j
```

第一次构建会下载固定版本的 Buildroot 并烤盘，视网速要等一会儿，之后走本地缓存。

### 5. 跑测试

有 KVM 时：

```bash
ctest --test-dir build/test -R ^kernel_test_console_cpu1$ --output-on-failure
```

没有 `/dev/kvm` 时，`run_kernel_tests.cmake` 会把 `-accel kvm -cpu host` 换成 `-accel tcg -cpu max`，打印一条警告，并把单个测试的超时放宽到 100 秒（CTest 外层上限仍是 120 秒）。CI 有 KVM，行为和以前一样。

TCG 下不要一上来跑全套内核测试，`kernel_test_*` 每个都要完整启动一次，数量多、速度慢。先用 `-R` 挑单个测试确认镜像和启动链路没问题，再决定是否全量。CTest 的测试注册在 `build/test` 子目录，`--test-dir` 要指到那里。

### 6. 在 WSL 里启动

```bash
cmake --build build --target run
```

串口接在当前终端，`Ctrl+A` 然后 `X` 退出 QEMU。

### 7. 把镜像交给 Windows

WSL 的文件可以从资源管理器访问：

```text
\\wsl.localhost\Ubuntu-24.04\home\<用户名>\Cinux\build\boot
```

把 `cinux.img` 和 `ext2.img` 复制到 Windows 磁盘，再用 `run-windows.ps1 -Kernel ... -Disk ...` 启动。直接从 `\\wsl.localhost` 读取也能跑，但启动会慢一些。

### 嵌套虚拟化：让 WSL2 里也有 /dev/kvm

WSL2 默认没有 `/dev/kvm`，内核测试只能走 TCG。Windows 11 22H2 以上的较新 WSL 支持嵌套虚拟化：

```ini
# %UserProfile%\.wslconfig
[wsl2]
nestedVirtualization=true
```

保存后执行 `wsl --shutdown`，重新进入发行版，确认：

```bash
ls -l /dev/kvm
```

有设备节点且当前用户可读写，QEMU 就会走 KVM。宿主 CPU 的虚拟化没开、或者 WSL 版本太旧时，这个开关不生效，仍然回退 TCG。

## CMake 加速器选项

构建时可以用 `CINUX_QEMU_ACCEL` 指定加速器：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCINUX_QEMU_ACCEL=auto
```

| 取值 | 行为 |
|------|------|
| `auto`（默认） | 按平台顺序挑第一个可用项：Linux 是 KVM → TCG，Windows 是 WHPX → TCG，macOS 是 HVF → TCG |
| `kvm` | 强制 KVM，不可用就回退 TCG 并警告 |
| `whpx` | 强制 WHPX，不可用就回退 TCG 并警告 |
| `hvf` | 强制 HVF，不可用就回退 TCG 并警告 |
| `tcg` | 强制软件模拟 |

CPU 参数跟着加速器走：KVM 和 HVF 用 `-cpu host`，WHPX 和 TCG 用 `-cpu max`。

检测方式是在配置阶段执行 `qemu-system-x86_64 -accel help`，再对 KVM 额外检查 `/dev/kvm` 是否存在且可读写。这份结果是配置时的快照，升级 QEMU 或换了加速器之后要重新跑一次 `cmake`。配置输出会打印一行最终选择，例如：

```text
-- QEMU acceleration: no hardware accelerator usable on Linux; falling back to tcg (emulated, slow)
```

## 限制与已知问题

- WHPX 路径没有在真实 Windows 机器上跑过。本次施工在 Linux 上完成，`run-windows.ps1` 只做了语法自查。探测逻辑本身在 Linux 上用 KVM/TCG 验证过（KVM 缺设备会立刻退出，TCG 会正常起机器）。第一次在 Windows 上跑如果 WHPX 起不来，先加 `-Accel tcg`，能起来就说明是加速器问题，不是镜像问题。
- WHPX 需要 Windows 11、BIOS 里开启虚拟化、启用「Windows 虚拟机监控程序平台」，并且 QEMU 8.0 以上。四条缺一条都会退 TCG。
- TCG 是逐条指令模拟，比 KVM 慢一到两个数量级。全套内核测试在 TCG 下会非常久，部分用例可能顶到超时。
- 运行层只有串口控制台。`run_gui` 依赖 GTK，Windows 原生 QEMU 没有这条显示路径，图形界面仍然留在 Linux。
- 编译层乙（MSVC / clang-cl / MinGW 直接构建）没有做，属远期。
- WSL1 不行。它没有真正的 Linux 内核设备，`/dev/kvm` 和 QEMU 加速都用不了。
- 不要在 `/mnt/c` 下构建。跨文件系统 I/O 慢，`mke2fs -d` 烤盘会明显拖时间。
- 当前 Release 页面只有源码归档，磁盘镜像要自己构建。

## 排错

| 现象 | 原因 | 处理 |
|------|------|------|
| `找不到 qemu-system-x86_64` | QEMU 没装或没进 PATH | 重装并勾选加入 PATH，或把 `C:\Program Files\qemu` 加进 PATH |
| `WHPX is not available` | Windows 功能没开或虚拟化没开 | 启用「Windows 虚拟机监控程序平台」，重启，或 `-Accel tcg` |
| 启动后黑屏、没有串口输出 | 镜像路径不对，或 `-Kernel` 指到了 ext2 盘 | 确认 `cinux.img` 和 `ext2.img` 没对调 |
| TCG 下测试超时 | 软件模拟太慢 | 挑单个测试，或在 WSL2 里开嵌套虚拟化用 KVM |
| WSL 里 `ls /dev/kvm` 不存在 | 没开嵌套虚拟化 | 按上面 `.wslconfig` 配置后 `wsl --shutdown` |
| `bash: scripts/windows/setup-wsl2.sh: Permission denied` | 脚本没有执行位 | 用 `bash scripts/windows/setup-wsl2.sh` 调用，或 `chmod +x` |
| CMake 报 GCC 版本不够 | 默认编译器低于 14 | 用 Ubuntu 24.04，或手动指定 `-DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14` |
