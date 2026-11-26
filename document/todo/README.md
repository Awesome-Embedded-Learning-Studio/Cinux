# Cinux 开发路线

这是一组按顺序做的文档。目标是让 Cinux 一步一步能替代日常使用的 Linux 环境，并把 Windows 里好用的功能做成 Cinux 自己的能力。每一份文档对应一段可以完整做完、单独验收的工作，做完一份再进入下一份。

《可信命令行》到《选定发行版的用户态》是主线，每一级都建在上一级的检查之上。后面五份是扩展路线，各有进入条件，主线走到哪一步它们就能开始，每份文档的开头写清楚了。

从《基线》读起，按顺序往下做。每一份文档里的任务分好了步，同一步的可以按任意顺序做。

## 文档顺序

| 顺序 | 文档 | 做完之后 | 任务数 |
|---|---|---|---|
| 00 | [基线](./00-baseline.md) | 知道现在站在哪里：实测结果，以及源码里能直接看到的约束 | 1 |
| 01 | [可信命令行](./01-shell.md) | 管道、重定向、文件工具、前后台作业和基本权限像参考 Linux 一样工作 | 29 |
| 02 | [原生开发工作站](./02-workstation.md) | 固定的开发工具和有限并行构建稳定工作，崩溃能定位 | 42 |
| 03 | [多用户服务系统](./03-services.md) | 非特权用户登录，服务自启动自恢复，网络和可恢复的存储 | 45 |
| 04 | [独立桌面](./04-desktop.md) | 多个独立图形程序、中文输入、剪贴板和受隔离的浏览器 | 23 |
| 05 | [容器运行环境](./05-containers.md) | 展开的根文件系统在隔离里运行，资源限制真实生效 | 10 |
| 06 | [选定发行版的用户态](./06-distribution.md) | 明确版本的 Linux 软件包、服务、桌面和容器工具能长期使用 | 11 |
| 07 | [Windows 里好用的功能](./07-windows-features.md) | 事件等待、进程树管理、完成式异步 I/O，以及服务和桌面的顺手体验 | 15 |
| 08 | [编译 Linux，运行虚拟机](./08-linux-and-vms.md) | Cinux 内编出 Linux，原生 QEMU 启动它，再启动 Ubuntu | 13 |
| 09 | [在 Cinux 里编译 Chromium](./09-chromium-build.md) | 浏览器在 Cinux 内全量、增量编出来 | 2 |
| 10 | [运行 Windows 程序](./10-windows-programs.md) | 受控的 PE64 样本和固定版本的 Win32 程序原生运行 | 6 |
| 11 | [硬件虚拟化](./11-hardware-virtualization.md) | 给 QEMU 一个真正的加速后端 | 4 |

## 怎么读一项任务

```
- [x] **冻结并重验起点**（测试与证据）：要做什么。验收：做到什么算完。
```

- 每项任务用自己的名字指代，名字在全组文档里唯一，不给任务编号。
- 前置是本文档里的任务时直接写名字。是别的文档里的任务时写成“哪份文档里的哪件事”，并带上链接。
- 名字后面括号里先写由谁实现：内核机制、Linux 接口、用户态库、用户态组件、测试与证据。它说明责任在哪一层，不是要新建的目录。
- 勾选表示实现和验收证据都齐了。源码里找到实现不算，旧文档里的成功记录也不算。
- 每项只交付一件事。一项明显偏大时，开工先把它继续分成约定、第一条成功路径、拒绝与清理、并发压力几个子项。
- 探针型的任务可以完成于“依赖和失败原因已经明确”，但后面开工之前要把探针发现的硬依赖登记成具体任务。

## 一项任务怎样才算完成

每项任务至少要有五样东西：

1. 成功行为：数据、返回值、状态变化符合《可信命令行》开头定下的参考环境。
2. 拒绝和故障行为：坏指针、权限不足、非法参数、内存、描述符、磁盘不足和 I/O 失败里，与这项任务有关的场景要正确失败。
3. 并发和退出清理：关闭、取消、信号、fork、exec、退出与操作竞争时不死锁、不越权、不泄漏。
4. 组件边界：没有让用户态界面代替内核强制，也没有把服务策略塞进系统调用或者驱动。
5. 证据：Cinux 和参考环境的版本、镜像配置、测试命令、输出、退出码和资源变化。PID、地址、时钟这类不确定的值先规范化再比较。

测试结果分通过、失败、未实现、环境不具备、范围外，skip 不算通过。明确范围内的安全失败，不能用跳过、空实现返回成功或者忽略上游错误来过关。长跑时间、负载和硬件在开工时定下，不能测完再降。

每个程序都要标明它是怎么跑起来的：原版 Linux ELF 直接运行、为 Cinux 重编译、专用适配，还是在虚拟机里运行。四种结果分开记，不混算。

## 组件边界：内核管机制，用户态管策略

保留模块化内核，不为了组件化先改成微内核。组件化要求的是职责、依赖和故障边界清楚，不要求第一天就实现动态内核模块，也不把现有的网络栈全部迁出内核。

| 组件 | 必须承担的职责 | 不应承担的职责 |
|---|---|---|
| 内核进程与调度模块 | 线程和进程的生命周期、等待唤醒、信号、调度、资源归属和执行限额 | 服务依赖图、应用自动重启、窗口管理 |
| 内核内存模块 | 页所有权、地址空间、共享对象、页缓存、回收、换页、访问保护、内存耗尽的最终处置 | 浏览器标签页策略、应用缓存目录清理 |
| 内核凭据与安全模块 | UID、GID、补充组、对象访问检查、特权操作、权限下降、系统调用过滤 | 密码数据库、登录界面、交互授权提示 |
| 内核文件与描述符模块 | 路径解析、文件描述符、打开文件对象、权限入口、挂载与后端生命周期 | 包依赖解析、安装向导、用户目录约定 |
| 文件系统和块设备后端 | ext2、ext4、FAT 等磁盘语义，读写、刷新、错误传播，块队列与驱动 | 把 ext4 的特例写进每个系统调用，忽略后端不支持的操作 |
| 网络与设备模块 | 套接字、协议与路由机制、设备访问、中断与 DMA、设备事件 | DHCP 租约策略、DNS 配置、证书更新、音量面板 |
| Linux 接口入口 | 结构体、调用号、标志、错误码、ioctl 的校验与翻译，再调用公共模块 | 再造一套 Linux 专用的页管理器、调度器或者文件系统 |
| libc、原生 SDK 与平台库 | 标准接口封装、可证明正确的用户态回退、平台适配 | 依赖程序自愿使用 SDK 才获得安全隔离 |
| 用户态基础服务 | init、服务管理、日志、登录、设备策略、网络配置、校时 | 直接修改页表、绕过内核授权、只在界面上显示限额 |
| 用户态桌面组件 | 合成器、窗口协议、字体、输入法、剪贴板、文件选择、音频服务 | 在内核里实现 Windows 的窗口和绘图接口、浏览器绘制 |
| 用户态软件管理 | 软件仓库、签名、依赖、文件归属、安装升级回滚 | 在内核里解析包格式、仓库地址或者安装脚本 |
| Windows 平台库与兼容组件 | 事件、完成队列和应用进程树的接口，PE、Wine，应用设置 | 再造 NT 内核，或者把 Wine、注册表、Windows 图形界面放进内核 |
| 用户态 QEMU 与浏览器 | CPU 模拟、虚拟设备、网页引擎、协议与应用功能 | 把 QEMU 或者 Chromium 链接进内核 |

组件之间的共同要求：

- 所有权和关闭规则明确，取消、超时、进程退出之后资源能归还。跨进程通信校验长度、版本和身份，不信任客户端的指针和句柄。
- 锁、页、描述符、挂载的生命周期分别由所属模块管理，调用者通过受约束的接口使用。
- 原生接口、Linux 接口和 Windows 风格的库共用同一组内核机制。Job 和 cgroup 共用资源归属与限额的执行点，任务管理器直接用公共资源组。
- 初期允许静态注册的可替换后端。动态装卸内核代码、稳定的内部模块接口、用户态驱动框架另立研究。
- 每个用户态服务有最小权限、控制接口、状态查询、日志、停止和崩溃恢复的规则。

## 必备能力与最晚的等级

必备不等于第一级全做完。下表约束每项能力最晚在哪一级必须具备，不能因为当前选用的软件恰好没调用它，就从目标里删掉。

| 能力 | 最晚必须具备 | 内核与用户态分工 |
|---|---|---|
| 内存、描述符、任务、队列耗尽可控，进程故障不终止内核 | 第 1 级建立失败路径，第 2 级压力验收 | 内核保证失败、取消、回收，工具负责报告 |
| 正确的文件生命周期、路径、权限、身份与进程授权 | 第 1 级基本授权，第 3 级完整多用户 | 内核强制，用户态登录和账户解析 |
| 线程信号隔离、可中断等待、线程组退出、作业控制 | 第 1 级作业控制，第 2 级多线程 | 内核机制，shell 和线程库使用 |
| 匿名共享页、文件共享映射、页缓存与有界回收 | 第 2 级 | 内核持有页与回收规则，应用选择使用方式 |
| 交换空间与超过内存的工作集 | 第 3 级 | 内核换入换出，用户态配置与观测，不能拿它代替内存耗尽的安全 |
| 描述符和凭据传递、事件循环、定时器、文件变化通知 | 第 3 级之前 | 内核对象和唤醒，库与服务组织协议 |
| 真实的 CPU、内存、I/O、进程数统计与强制限制 | 第 3 级 | 内核统计与执行，任务管理器只展示和发管理请求 |
| 可写的大文件和大卷、正确的同步与错误传播 | 第 2 级，ext4 的恢复在第 3 级 | 文件层、后端、块层各守边界，修复工具在用户态 |
| ext4、可恢复的持久数据盘与根盘 | 第 3 级 | 文件系统组件和修复工具配套 |
| 可配置的 IPv4、IPv6、DNS、时钟、可信随机数、TLS | 第 3 级 | 协议和权限在内核，DHCP、DNS、NTP、TLS 和证书在用户态 |
| 服务生命周期、软件管理、设备事件、关机重启 | 第 3 级 | 用户态服务为主，内核给受控的设备和电源接口 |
| 独立窗口、共享缓冲、输入、字体和客户端隔离 | 第 4 级 | 内核提供安全的 IPC 和设备，桌面组件在用户态 |
| 命名空间、资源组、权限下降、系统调用过滤 | 第 4 级沙箱按配置具备，第 5 级完成容器范围 | 隔离与限制由内核强制，启动器只配置 |
| proc、sysfs、cgroup 的视图，动态链接，NSS，发行版工具 | 第 6 级，之前按等级逐步补齐 | 内核导出真实状态，策略和数据库在用户态 |

io_uring、eBPF、NUMA、实时调度、全部 Linux 文件系统和所有 GPU 驱动不一概列为前置。目标软件明确依赖时，按同样的方式补成带验收的任务。

## 扩展路线的进入条件

| 文档 | 可以进入的条件 | 不必等待 |
|---|---|---|
| [Windows 里好用的功能](./07-windows-features.md) | 事件等待要《可信命令行》的「统一等待与取消约定」和《原生开发工作站》的 futex、eventfd。进程树的限额要《多用户服务系统》的资源组控制器，应用进程树和桌面上的体验要《独立桌面》 | PE、Wine、注册表、Windows 图形界面 |
| [编译 Linux，运行虚拟机](./08-linux-and-vms.md) | 构建工具和可靠工作卷在《原生开发工作站》就绪。QEMU 的运行时探针只要那一份里的线程、信号、内存和定位 I/O | QEMU 本地编译、Ubuntu、GPU、硬件加速 |
| [在 Cinux 里编译 Chromium](./09-chromium-build.md) | 《独立桌面》的多进程浏览器，《选定发行版的用户态》的大内存和发行版工具链 | GPU 加速 |
| [运行 Windows 程序](./10-windows-programs.md) | PE64 实验要《可信命令行》的 exec 和《原生开发工作站》的内存。Wine 要《原生开发工作站》的进程间通信、信号上下文和软件包，图形程序再要《独立桌面》 | Windows 驱动、.NET、DirectX |
| [硬件虚拟化](./11-hardware-virtualization.md) | 《多用户服务系统》的电源和启动路径，《编译 Linux，运行虚拟机》的 Ubuntu 串口启动 | 容器、Chromium |

## 资料

上游的 main 和 master 文档会变化。定参考环境的时候固定修订，不照搬它们全部的最新要求当作第一级的前置。

### 行为规范、服务与隔离

- [POSIX 系统接口索引](https://pubs.opengroup.org/onlinepubs/9799919799/idx/functions.html)
- [LTP 单测试与 Open POSIX 入口](https://linux-test-project.readthedocs.io/en/latest/users/quick_start.html)
- [Linux 凭据与身份](https://www.man7.org/linux/man-pages/man7/credentials.7.html)
- [Linux namespaces](https://www.man7.org/linux/man-pages/man7/namespaces.7.html)
- [cgroup v2 的层级和执行语义](https://docs.kernel.org/admin-guide/cgroup-v2.html)
- [systemd 要求与可选功能](https://raw.githubusercontent.com/systemd/systemd/main/README)
- [OCI Linux 配置与特殊路径](https://raw.githubusercontent.com/opencontainers/runtime-spec/main/config-linux.md)

### 存储、构建和虚拟机

- [Linux 6.12 构建要求](https://www.kernel.org/doc/html/v6.12/process/changes.html)
- [Linux Kconfig](https://www.kernel.org/doc/html/v6.12/kbuild/kconfig.html)
- [Linux 维护版本](https://www.kernel.org/category/releases.html)
- [ext4 结构](https://docs.kernel.org/filesystems/ext4/overview.html)
- [ext4 与 JBD2 日志、恢复](https://docs.kernel.org/filesystems/ext4/journal.html)
- [QEMU 系统模拟与加速](https://qemu-project.gitlab.io/qemu/system/introduction.html)
- [QEMU TCG 执行模式](https://qemu-project.gitlab.io/qemu/devel/multi-thread-tcg.html)
- [QEMU 直接启动 Linux](https://qemu-project.gitlab.io/qemu/system/linuxboot.html)
- [QEMU 构建环境](https://qemu-project.gitlab.io/qemu/devel/build-environment.html)
- [QEMU 构建系统](https://qemu-project.gitlab.io/qemu/devel/build-system.html)
- [Ubuntu Noble 云镜像索引](https://cloud-images.ubuntu.com/noble/current/)
- [cloud-init QEMU 示例](https://docs.cloud-init.io/en/26.1/howto/launch_qemu.html)
- [KVM 接口](https://docs.kernel.org/virt/kvm/api.html)

### Windows 功能与程序兼容

- [Windows 等待语义](https://learn.microsoft.com/en-us/windows/win32/sync/wait-functions)
- [Windows 完成端口](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports)
- [Windows Job 管理](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)
- [PE/COFF 格式](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
- [Microsoft x64 ABI](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170)
- [Wine 架构](https://gitlab.winehq.org/wine/wine/-/wikis/Wine-Developer's-Guide/Architecture-Overview.md)
- [Wine 构建与 WoW64](https://gitlab.winehq.org/wine/wine/-/wikis/Building-Wine.md)
- [Wine 进程间通信实现](https://gitlab.winehq.org/wine/wine/-/raw/master/dlls/ntdll/unix/server.c)
- [Wine 信号与异常实现](https://gitlab.winehq.org/wine/wine/-/raw/master/dlls/ntdll/unix/signal_x86_64.c)
- [Wine 许可证](https://gitlab.winehq.org/wine/wine/-/raw/master/LICENSE)

### 浏览器

- [Chromium Linux 构建](https://raw.githubusercontent.com/chromium/chromium/main/docs/linux/build_instructions.md)
- [Chromium Ozone 平台接口](https://raw.githubusercontent.com/chromium/chromium/main/docs/ozone_overview.md)
- [Chromium Linux 沙箱](https://chromium.googlesource.com/chromium/src/+/HEAD/sandbox/linux/README.md)
- [Chrome headless](https://developer.chrome.com/docs/automation-and-testing/headless)
