# 00 基线：现在站在哪里

这份文档回答两件事：实测的结果是什么，源码里能直接看到的约束有哪些。这里只有一项任务，已经做完。接下来从[《可信命令行》](./01-shell.md)开始。

- [x] **冻结并重验起点**（测试与证据）：记录镜像、工具链、配置与现有测试结果。验收：启动、动态 ELF、pthread、文件/网络和自编译各有实际结果，失败与未执行分别登记。2026-11-26 完成，TCG 和 KVM 各跑一遍，证据在 `build-baseline`。

日期：2026-11-26。源码：`e12eb3b5ced0e288fdb48bccc8c7afc3e4b7fbd8`（分支 `feat/next-todo`，内核相对 `cba386e` 无变化，工作树只有路线图改动）。

本记录只写本轮实际执行得到的结果。没有执行的项目单列在第 7 节，不用旧文档里的成功记录代替。

基线跑了两遍。第一遍在会话的受限文件策略下，`/dev/kvm` 被隐藏，`/var/tmp` 只读，客体全部走 QEMU TCG。放开权限之后第二遍用 KVM 重跑，仓库自带的客体测试入口也因此能跑。两遍的内核行为一致，差异只在外网能不能测、官方入口能不能用。

## 1. 环境

| 项目 | 值 |
|---|---|
| 开发电脑内核 | `6.18.33.2-microsoft-standard-WSL2` |
| `/dev/kvm` | 放开后可用，`crw-rw-rw-`，CPU 带 `vmx`。configure 自动选到 `kvm`，`-cpu host` |
| 宿主工具 | GCC 16.2.1，glibc 2.44，CMake 4.5.1，Ninja 1.13.2，QEMU 11.1.3，Python 3.14.10 |
| 宿主外网 | 可用，`https://github.com` 返回 200，0.47 秒 |
| 资源 | 20 个逻辑 CPU，15 GiB 内存，工作区所在盘剩余约 887 GiB |
| 构建目录 | `build-baseline`（新建，Release） |

构建配置与仓库 `scripts/checks/run-cell.sh` 的 Release 单元一致：`-O2 -DNDEBUG`，宿主 ASan/TSan 和内核 UBSan/lockdep 全部关闭。两处偏离：编译池 `CINUX_COMPILE_JOBS=12`，宿主侧 QEMU 的 slirp 唤醒辅助库 `CINUX_QEMU_SLIRP_WAKEUP=OFF`。客体 2 个虚拟 CPU，1024 MiB 内存。

产物：

| 文件 | 大小 | SHA-256 |
|---|---|---|
| `build-baseline/kernel/kernel.bin` | 721,840 字节 | `fa675610d187a2410dfb33e794fe8c020cc8faec05504a1f734c0009a327836a` |
| `build-baseline/boot/cinux.img`（单盘：引导链 + 内核 + 开发卷） | 1,611,661,312 字节 | `208840abcb855762b72f37a1a67ac6174adb29a6a7d3d62381b00a21ed7d15a9` |
| `build-baseline/boot/cinux-boot.img`（双盘测试入口的启动盘，`boot_image` 目标生成） | 738,304 字节 | 由 `test_guest` 目标生成，两遍之间未重建 |

`card_image` 和 `test_host` 两个目标一起构建，从 configure 到镜像生成约 6 分钟。

## 2. 结果总表

| 范围 | 第一遍 TCG | 第二遍 KVM |
|---|---|---|
| 宿主单元测试（`test_host`：单元 + 架构边界 + 内核镜像检查） | 85 项全部通过 | 同一套构建，未重跑 |
| 内核测试镜像（`test_kernel`，41 个 QEMU 启动） | 41 项全部通过 | 41 项全部通过 |
| 官方客体入口 `test_guest` + `ctest -R '^guest_'` | 没跑，`run-cell.sh` 当时因缺 KVM 拒绝 | 5 项全部通过 |
| 单盘启动到开发用户 shell | 通过 | 通过 |
| 动态 glibc ELF（`hello-glibc`，eco 卷） | 通过 | 通过 |
| 文件与基础命令 | 通过 | 通过 |
| 持久化：写入、sync、关机、重启后读回 | 通过 | 通过，官方 `test_persistence.py` 3 个周期也通过 |
| 网络：网关 ping、丢包超时、DNS、ifconfig、TCP 回环 | 通过 | 通过 |
| 外网：ping `1.1.1.1`、解析 `github.com`、HTTP 取 `example.com` | 没测 | 通过（root） |
| 开发工具：cmake、ninja、python3、nativeprobe、gcc、g++ | 通过 | 通过 |
| pthread | **失败**：`pthread_create` 返回 EINVAL | 相同 |
| 管道写端退出 | **挂起**：`yes | head -1` 不返回，Ctrl-C 救不回 shell | 相同，`wget | head -c 200` 复现 |
| 后台任务等待 | **挂起**：`sleep 1 & wait` 不返回 | 相同 |
| 自编译 | **失败**：客体里 `cmake -B build -S .` 在 configure 阶段中断 | 相同 |
| 普通用户 ping | **失败**：iputils `ping` 报 permission denied | 相同，官方 `test_native_build.py` 也卡在这里 |
| 官方 `test_native_build.py` | 没跑 | 补上 `guest.py` 缺的 import 后，在 `ping -c 2 127.0.0.1` 一步失败 |

## 3. 官方测试入口

放开权限后，仓库自己的入口能跑的都跑了。

| 入口 | 结果 |
|---|---|
| `cmake --build build-baseline --target test_host` | 85 / 85，0.46 秒 |
| `cmake --build build-baseline --target test_kernel` | TCG 41 / 41，KVM 41 / 41 |
| `cmake --build build-baseline --target test_guest` | 构建退出码 0，生成 `boot/cinux-boot.img` |
| `ctest --test-dir build-baseline -R '^guest_'` | 5 / 5 通过，共 110 秒：ecosystem 1.93 秒，selfhost 6.67 秒，net_ping 73.08 秒，net_tcp 3.36 秒，desktop 24.89 秒 |
| `test/test_native_build.py --image build-baseline/boot/cinux-boot.img --configure-only` | 原样运行：`NameError: name 're' is not defined`。在内存里给 `guest` 模块补上 `re` 和 `os` 再跑：`ping -c 2 -W 2 127.0.0.1` 返回 1，脚本按失败退出，没走到 configure |
| `test/test_persistence.py --image build-baseline/boot/cinux-boot.img` | 同样补 import 后运行：3 个周期全部通过，每个周期构建、sync、关机、fsck 并提取内容比对 |

`test_native_gui.py`、带 `--gui` 的持久化和 `test_gui_tab.py` 也要 import `guest.py`，没有运行。

## 4. 直接探针

官方入口之外，`build-baseline/baseline-guest.py` 直接启动 `boot/cinux.img`，从串口输入命令，以 `BASE_<名称>_$?` 行取退出码。TCG 和 KVM 各跑一遍，原始串口日志和逐项结果在 `build-baseline/baseline/`，文件名带 `-tcg` 或 `-kvm`。

### 4.1 eco 卷（root shell，同一内核配 `boot/cinux-eco.img`）

`hello-glibc` 输出 `Hello from dynamic glibc on Cinux!`。uname、id、whoami、pwd、env、hostname、free、`wc /etc/passwd` 通过。`/proc/1/stat` 是 `1 (init)`。`mount -a` 后 `/proc/mounts` 有 `none /tmp tmpfs rw 0 0`。`ecotest --guest` 输出 `ECOSYSTEM_PASS shm unix poll relro`。`echo pipe | cat` 通过。

网络：`ping -c 3 -W 2 10.0.2.2` 三发三收。`ping -c 1 -W 1 10.0.2.254` 输出 `1 packets transmitted, 0 packets received`，退出码 1 是 iputils 对丢包的常规返回，探针脚本按退出码 0 判定才把它记成 fail。`nslookup localhost` 通过。KVM 下另外测了外网：`ping -c 3 -W 3 1.1.1.1` 三发三收，`nslookup github.com` 解析出地址，`busybox wget -q -O - http://example.com` 取回了页面。

`/bin/smpstress` 输出 `SMP_FORK_PASS cpus=2 rounds=96`。

### 4.2 开发卷（`cinux` 用户，uid 1000）

`uname -a` 是 `Cinux cinux 0.3.0 x86_64 GNU/Linux`。id、pwd、ps、free、`/proc/1/stat`、`/proc/mounts`、写 `/tmp`、`echo pipe | cat`、2 个 processor 全部通过。`mount -a` 对 uid 1000 返回 permission denied，这是预期行为。

cmake、ninja、python3 能执行，`/bin/nativeprobe` 通过，`gcc /hello.c`、`g++ /hello.cpp`（含 C++ 异常捕获）通过。ifconfig、`nslookup localhost` 通过。TCP 回环（busybox nc 监听 4410，客户端发送 hi）客户端和服务端退出码都是 0。

### 4.3 持久化

在 `cinux.img` 的副本上不加 `-snapshot`：第一次启动写 `~/.cinux-baseline-check` 并 `sync`，`poweroff` 退出码 0。第二次启动读回 `persisted`，`poweroff` 退出码 0。TCG 和 KVM 各做了一遍。

### 4.4 网络身份差异

root 下 iputils `ping` 正常。开发用户下同一个 `ping` 报 `ping: permission denied (are you root?)`。用 python3 单独探测：`SOCK_DGRAM` + `IPPROTO_ICMP` 的 socket 创建成功，`SOCK_RAW` 返回 `Operation not permitted`。README 里“普通用户可直接 ping”的说法在本基线不成立。

## 5. 发现清单

按对后续任务的影响排列。每一条 TCG 和 KVM 都复现过。

### 5.1 pthread_create 返回 EINVAL

开发卷里的 glibc 2.44 运行库下，4 线程程序 `pthread_create` 返回 22（Invalid argument），期间内核没有打印 unserved syscall。clone 被服务了，但 glibc 传入的标志组合被拒绝。另外 `-pthread` 链接本身就失败：`/usr/bin/ld: cannot find -lpthread`，开发 overlay 没有带 libpthread 的桩库。不带 `-pthread` 能链接，运行时才失败。

对应《原生开发工作站》里的「标准线程创建路径」，工具链缺项记在同一份文档的「工具链和开发依赖集合」。

### 5.2 管道读者退出后写端挂起，Ctrl-C 无效

`yes | head -1` 输出一行 `y` 后再没有返回，45 秒内没有退出标记，发送 Ctrl-C 后 30 秒内 shell 提示符没有回来。KVM 下 `busybox wget -q -O - http://example.com | head -c 200` 取回了页面的前 200 字节，然后同样挂住，说明不是 `yes` 的问题。对应《可信命令行》里的「管道、FIFO 与挂断」和「停止/继续与终端作业」。

### 5.3 `sleep 1 & wait` 挂起

BusyBox ash 的 `wait` 走 `rt_sigsuspend`（x86_64 调用号 130），内核未实现并打印 `[kern] syscall 130 unserved`。45 秒内 TCG 打印 24,254 次，KVM 打印 49,339 次，命令不返回，Ctrl-C 同样救不回 shell。对应《可信命令行》里的「rt_sigsuspend 与 shell 的 wait」「可中断阻塞与重启」「停止/继续与终端作业」。

### 5.4 自编译在 configure 阶段失败

客体里 `cmake -B build -S .` 报：

```
CMake Error at cmake/rootfs.cmake:63 (file):
  file COPY cannot set modification time on
  "/home/cinux/Cinux/build/ktest-fixture/hello.txt": Function not
  implemented.
```

`utimensat`（x86_64 调用号 280）未实现。ktest 夹具的复制步骤要求保留时间戳，CMake 在 configure 阶段就退出。自编译在本基线不可用，后续构建、增量、产物启动都没有执行。对应《可信命令行》里的「完整文件元数据」。

### 5.5 普通用户 ping 失败，官方自编译脚本被它挡住

见 4.4。`test_native_build.py` 以 `cinux` 用户登录，探针里的 `ping -c 2 -W 2 127.0.0.1` 返回 1，脚本按失败退出。对应《多用户服务系统》里的「套接字通用行为」。

## 6. 测试入口本身的状态

- `test/guest.py` 用到了 `re` 和 `os`，没有 `import`，`wait_for()` 一调用就是 `NameError`。受影响的是 `test_native_build.py`、`test_native_gui.py`、`test_persistence.py`、`test_gui_tab.py`，这四个脚本不在 ctest 里，由 `run-cell.sh native` 直接调用。
- `test/native-build-proof.py` 在 `kernel/kernel.cpp` 里找启动横幅，横幅现在在 `kernel/arch/x86_64/arch_init.cpp:28`。只读了源码，没有运行，因为 `--stamp` 会改工作树里的内核源码。
- `scripts/checks/run-cell.sh` 对 host 以外的单元强制要求 `/dev/kvm`。受限策略下它直接拒绝，放开后这一条不再拦路。
- 并行跑多个客体时，共用 `boot/nvme-scratch.img` 的两个 QEMU 会因为写锁冲突启动失败，探针脚本因此改成串行。
- `boot/cinux-boot.img` 仍然由 `boot_image` 生成，双盘形态的五个 `guest_*` 测试在 KVM 下全部通过。单盘形态的 `cinux.img` 目前只被 `run*` 目标和本轮的直接探针使用。

对应《可信命令行》里的「修复原生测试脚本的入口」。

## 7. 没有执行的项目

| 项目 | 原因 |
|---|---|
| `test_native_gui.py`、带 `--gui` 的持久化、`test_gui_tab.py` | 依赖 `guest.py` 的缺失 import，要等《可信命令行》里的「修复原生测试脚本的入口」 |
| 自编译的构建、增量、产物启动 | 被 5.4 的 configure 失败挡住 |
| 开发用户下的外网 ping | 被 5.5 挡住 |
| ARMv7 构建与客体测试 | 本机没有 arm-none-linux-gnueabihf 工具链，不在本次范围 |
| ASan/TSan/UBSan/lockdep 单元 | 本基线只跑 Release 无插桩配置 |

`guest_desktop`（Vesper 帧缓冲截图比对）在官方入口里通过了，没有显示环境不影响它。

## 8. 证据与复现

所有证据在 `build-baseline/`，该目录被 `.gitignore` 的 `/build*/` 排除，不入库：

- `configure.log`、`configure-kvm.log`、`build.log`（含 `test_host` 的 ctest 输出）、`ktest.log`（TCG）、`ktest-kvm.log`
- `baseline/results-*-tcg.json`、`baseline/results-*-kvm.json`：逐项结果。`baseline/*-tcg.serial.log`、`baseline/*-kvm.serial.log`：每次启动的完整串口记录
- `baseline/official-guest-lane.log`、`official-native-build.log`（补 import）、`official-native-build-unpatched.log`（原样）、`official-persistence.log`
- `baseline/unserved-syscalls-tcg.json`、`unserved-syscalls-kvm.json`：各次启动里内核打印的 unserved 调用号及次数。观察到 130（rt_sigsuspend）、137（statfs）、280（utimensat）
- `baseline-guest.py`：探针脚本

复现顺序：

```sh
cmake -S . -B build-baseline -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
      -DCINUX_HOST_ASAN=OFF -DCINUX_HOST_TSAN=OFF -DCINUX_UBSAN=OFF -DCINUX_LOCKDEP=OFF \
      -DCINUX_COMPILE_JOBS=12 -DCINUX_QEMU_SLIRP_WAKEUP=OFF \
      -DCMAKE_C_COMPILER_LAUNCHER="" -DCMAKE_CXX_COMPILER_LAUNCHER=""
cmake --build build-baseline --parallel 12 --target card_image test_host
cmake --build build-baseline --target test_kernel test_guest
ctest --test-dir build-baseline -R '^guest_' --output-on-failure
python3 scripts/create/assemble_image.py --layout scripts/create/meta/cinux-x86_64.json \
      --output build-baseline/boot/cinux-eco.img --source mbr=$PWD/build-baseline/kernel/arch/x86_64/boot/mbr.bin \
      --source stage2=$PWD/build-baseline/kernel/arch/x86_64/boot/stage2.bin \
      --source kernel=$PWD/build-baseline/kernel/kernel.bin --source volume=$PWD/build-baseline/boot/eco.img
# 客体探针要串行跑，共用的 nvme-scratch.img 不能被两个 QEMU 同时打开
for mode in probe eco extra pthread hangs selfbuild persist scan; do
  python3 -B build-baseline/baseline-guest.py $mode --build build-baseline --tag kvm
done
```

## 9. 源码里能直接看到的约束

下面这些是读源码看到的实现边界，用来决定先做什么。复用已有实现，不把它们重新说成不存在。最后一列写承接它们的任务在哪份文档里。

| 当前情况 | 下一步 | 去向 | 源码证据 |
|---|---|---|---|
| 已有 clone、TLS 和线程组相关的标志，但 CLONE_SIGHAND 共享的是包含掩码和 pending 的整个状态 | 分清处置表、线程掩码、线程待决和进程待决，再扩完整的信号帧 | 《可信命令行》里的「信号状态归属」「标准信号帧与备用栈」 | [clone](<../../kernel/proc/clone.cpp#L24-L39>)、[信号状态](<../../kernel/proc/signals.hpp#L64-L78>) |
| 写时复制的写缺页分配失败会触发内核 Check，而且先改引用计数再分配 | 在扩大负载之前建立不破坏状态的内存失败路径 | 《原生开发工作站》里的「缺页和 COW 失败安全」 | [写时复制缺页](<../../kernel/arch/x86_64/cow_resolve.cpp#L110-L125>) |
| 按需分页、私有映射和共享页的基础已有，匿名共享 mmap 被拒绝 | 复用共享对象，依次做匿名共享、memfd、页缓存和文件共享映射 | 《原生开发工作站》里的「匿名共享映射」到「可写共享、脏页与同步」 | [mmap](<../../kernel/syscall/sys_mmap.cpp#L25-L48>)、[匿名分支](<../../kernel/syscall/sys_mmap.cpp#L78-L87>) |
| access 只查存在，stat 没有交付完整的属主，身份接口不等于强制隔离 | 贯通元数据、公共路径检查和管理操作授权 | 《可信命令行》里的「完整文件元数据」和「凭据和元数据贯通」到「进程和管理操作鉴权」 | [access](<../../kernel/syscall/sys_access.cpp#L15-L27>)、[stat](<../../kernel/syscall/sys_stat_record.cpp#L41-L60>) |
| ext2 固定 1 KiB 块，直接、一级、二级映射约 64.26 MiB，块组规模约 2 GiB | 先补通用的文件语义，再扩可靠的工作卷，同时推进 ext4 后端 | 《原生开发工作站》里的「全链路大偏移」到「块 I/O 故障与持久化顺序」，《多用户服务系统》里的 ext4 各项 | [布局](<../../kernel/fs/ext2/ext2_layout.hpp#L41-L74>)、[映射](<../../kernel/fs/ext2/ext2_layout.cpp#L97-L128>)、[挂载检查](<../../kernel/fs/ext2/ext2.cpp#L82-L96>) |
| mount 只接受 tmpfs，目录 rename 还受限 | 通用的挂载生命周期和目录操作 | 《可信命令行》里的「链接与目录重命名」，《原生开发工作站》里的「通用挂载生命周期」 | [mount](<../../kernel/syscall/sys_mount.cpp#L36-L62>)、[rename](<../../kernel/fs/ext2/ext2_write.cpp#L449-L467>) |
| AF_UNIX、sendmsg 和 epoll 已有，控制消息、边沿触发和单次通知的范围不足 | 分别补描述符传递和通知语义 | 《原生开发工作站》里的「epoll 的完整目标语义」「SCM_RIGHTS 描述符传递」「UNIX 套接字身份与名字」 | [sendmsg](<../../kernel/syscall/sys_socket.cpp#L560-L571>)、[epoll](<../../kernel/syscall/sys_epoll.cpp#L288-L302>) |
| prlimit 已经保存限制，部分限制有执行点，proc 里的 CPU 和内存统计还是零 | 统一真实的统计，再逐项接执行点 | 《原生开发工作站》里的「CPU 与时间统计」「内存与 I/O 统计」「procfs 进程视图」，《多用户服务系统》里的「单进程限额执行」和三个资源组控制器 | [prlimit](<../../kernel/syscall/sys_prlimit.cpp#L32-L61>)、[进程统计](<../../kernel/fs/procfs/procfs.cpp#L81-L117>) |
| 默认运行内存 1 GiB，物理页覆盖到 16 GiB 地址 | 容量测试和内存策略分开做，后期再扩大物理地址覆盖 | 《选定发行版的用户态》里的「大内存与容量配置」 | [运行配置](<../../cmake/qemu/devices.cmake#L117-L123>)、[页管理配置](<../../kernel/mm/pmm_config.hpp#L41-L55>) |
| IPv4、TCP 和 UDP 已有，地址和路由绑定在 QEMU 的默认网上，TCP 并发容量较小 | 配置接口、动态容量和恢复 | 《多用户服务系统》里的「接口、地址和路由配置」「TCP 容量与压力恢复」 | [网络装配](<../../kernel/net/net_world.cpp#L148-L178>)、[TCP 容量](<../../kernel/net/net_config.hpp#L239-L272>) |
| 已有基础设备注册、块设备后端和 Vesper 桌面 | 沿已有接口扩后端和用户态协议 | 《多用户服务系统》里的「设备模型接口封口」到「固件与电源基本路径」，《独立桌面》里的「安全共享缓冲约定」到「Vesper 与标准平台连接」 | [设备模型](<../../kernel/drivers/device/device.hpp#L88-L105>)、[块接口](<../../kernel/drivers/block/block_device_concept.hpp#L61-L90>)、[桌面入口](<../../third_party/vesper/examples/fbdev_desktop.cpp#L374-L418>) |
