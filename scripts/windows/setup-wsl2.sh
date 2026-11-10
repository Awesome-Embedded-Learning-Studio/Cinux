#!/usr/bin/env bash
# 在 WSL2 里一次性搭好 Cinux 的编译环境：装依赖、配置 CMake、构建 test_host。
#
# 用法：bash scripts/windows/setup-wsl2.sh
#
# 可选环境变量：
#   CINUX_BUILD_DIR  CMake 构建目录，默认 <仓库>/build
#   CINUX_JOBS       并行任务数，默认 nproc
#
# 这套脚本面向 Ubuntu 24.04 LTS（WSL2）。Cinux 需要 GCC 14 及以上，
# Ubuntu 22.04 的官方源只有 GCC 13，别用旧发行版。
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="${CINUX_BUILD_DIR:-${repo_root}/build}"
jobs="${CINUX_JOBS:-$(nproc)}"

log() {
    printf '[cinux-wsl2] %s\n' "$*"
}

die() {
    printf '[cinux-wsl2] 错误：%s\n' "$*" >&2
    exit 1
}

# WSL 的 /proc/version 带 Microsoft 字样，WSL2 的内核版本还带 -WSL2。
if grep -qi microsoft /proc/version 2>/dev/null; then
    log "检测到 WSL：$(uname -r)"
    case "$(uname -r)" in
        *WSL2*) ;;
        *) log "警告：看起来是 WSL1。WSL1 没有真正的 Linux 内核设备，建议升级到 WSL2。" ;;
    esac
else
    log "警告：当前不是 WSL（$(uname -s)）。脚本仍按 Ubuntu 流程安装依赖，可以继续。"
fi

if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
fi
log "发行版：${PRETTY_NAME:-未知}"

command -v apt-get >/dev/null 2>&1 || die "没有 apt-get，本脚本只支持 Debian/Ubuntu 系。"

if [ "$(id -u)" -eq 0 ]; then
    sudo_cmd=()
else
    command -v sudo >/dev/null 2>&1 || die "需要 sudo 安装依赖。请先装 sudo，或者用 root 运行。"
    sudo_cmd=(sudo)
fi

# 依赖清单和 CI 的 .github/actions/setup-cinux 保持一致，多一个 make
# 方便直接手敲命令。e2fsprogs 提供 mke2fs，用来烤 ext2 根盘。
packages=(
    gcc-14 g++-14 gcc-14-multilib g++-14-multilib
    cmake ninja-build make binutils libc6-dev
    python3 qemu-system-x86 e2fsprogs file
    bc bison flex rsync cpio unzip iputils-ping
    libncurses-dev libssl-dev libarchive-dev pkg-config
)

log "更新 apt 索引并安装依赖（会用到 sudo）..."
"${sudo_cmd[@]}" apt-get update

if ! apt-cache show gcc-14 >/dev/null 2>&1; then
    die "软件源里没有 gcc-14。请换用 Ubuntu 24.04 或更新的发行版。"
fi

"${sudo_cmd[@]}" apt-get install -y "${packages[@]}"

cc="$(command -v gcc-14 || true)"
cxx="$(command -v g++-14 || true)"
if [ -z "$cc" ] || [ -z "$cxx" ]; then
    # 退一步用默认编译器，但版本不够就直接说清楚，不留给 CMake 报错。
    cc="$(command -v gcc || true)"
    cxx="$(command -v g++ || true)"
    [ -n "$cc" ] && [ -n "$cxx" ] || die "找不到 gcc/g++。"
    major="$("$cc" -dumpversion | cut -d. -f1)"
    [ "$major" -ge 14 ] || die "默认 GCC 是 $major，Cinux 要求 14 及以上。"
    log "没用上 gcc-14，改用默认编译器 $cc（GCC $major）。"
fi

log "配置 CMake 到 $build_dir ..."
cmake -S "$repo_root" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="$cc" \
    -DCMAKE_CXX_COMPILER="$cxx"

log "构建 test_host（主机端单元测试）..."
cmake --build "$build_dir" --target test_host -j "$jobs"

if [ -c /dev/kvm ] && [ -w /dev/kvm ]; then
    log "/dev/kvm 可用，QEMU 内核测试会走 KVM 硬件加速。"
else
    log "没检测到可用的 /dev/kvm，QEMU 内核测试会自动回退 TCG（软件模拟，慢）。"
    log "需要硬件加速时，在 Windows 侧打开嵌套虚拟化："
    log "  %UserProfile%\\.wslconfig 里写 [wsl2] 和 nestedVirtualization=true，再 wsl --shutdown。"
fi

cat <<EOF

[cinux-wsl2] 环境就绪。下一步：

  # 生成内核镜像和 ext2 根盘（第一次会下载 Buildroot，可能要等）
  cmake --build "$build_dir" --target boot_image ext2_img -j "$jobs"

  # 跑一个内核冒烟测试。CTest 会在没有 /dev/kvm 时改用 TCG
  ctest --test-dir "$build_dir/test" -R ^kernel_test_console_cpu1$ --output-on-failure

  # 在 WSL 里直接起系统（串口进控制台，Ctrl+A X 退出 QEMU）
  cmake --build "$build_dir" --target run

Windows 侧用 QEMU 直接跑同一个 build\boot\cinux.img：
  scripts\\windows\\run-windows.ps1
EOF
