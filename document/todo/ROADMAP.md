# Cinux 大应用与兼容能力里程碑

调研日期：2026-11-25。代码基线：`cba386e707b1fd1e4d3532ebfd90473a7e662949`。

状态：建议排期，尚未实施。本文依据当前源码与上游文档评估可行性，没有实际移植或编译 Linux、QEMU、Ubuntu、Wine、Chromium。历史自编译记录不等于这些新目标已经通过。

## 目标与推荐顺序

目标是在 Cinux 内编译 Linux，运行原生 QEMU 启动它，再运行 Ubuntu 和浏览器。长期把 Ubuntu 用户态兼容、Windows 实用接口及程序兼容、原生 Chromium 纳入系统能力。

建议主线如下，各项都有可单独验收的成果：

1. 扩大开发环境的文件、内存和进程承载能力。
2. 在 Cinux 内编出能启动的 Linux 内核。
3. 移植 QEMU-system 的 TCG 软件模拟后端，由 Cinux 启动 Linux 和一个最小根文件系统。
4. 在这台 QEMU 虚拟机里运行 Ubuntu，先命令行，再桌面和浏览器。
5. 并行推进 Cinux 原生 Ubuntu 程序子集、ext4、Windows 实用接口和独立图形应用。
6. 让 Chromium 的渲染与多进程功能原生运行在 Cinux，补好浏览器沙箱后访问公网。
7. 在 Cinux 内完成 Chromium 的本地构建。硬件虚拟化作为提升 Ubuntu 体验的独立研究项目推进。

这里的“原生”指进程直接由 Cinux 内核管理。“Ubuntu 虚拟机里的程序”由虚拟机内部的 Linux 内核管理，不能计入 Cinux 的原生程序兼容成绩。

## 先区分容易混淆的目标

| 目标 | 实际证明了什么 | 不能顺带宣称什么 |
|---|---|---|
| 在 Cinux 编译 Linux | Cinux 的开发环境能承载 Linux 构建系统和工具链 | Cinux 内核变成了 Linux，或已能运行 Ubuntu |
| Cinux 原生运行 QEMU-system | Cinux 能运行模拟 CPU、内存和设备的用户态程序 | 已有硬件虚拟化，或已经支持 Linux 全部系统调用 |
| QEMU 启动 Ubuntu | 一套 Ubuntu 系统通过 QEMU 和自己的 Linux 内核运行 | Ubuntu 的程序已直接运行在 Cinux 上 |
| 在 Cinux 直接运行 Ubuntu 程序 | 选定的 Linux 二进制接口和运行库行为兼容 | 完整 Ubuntu 桌面、systemd、Snap、容器均兼容 |
| Windows 实用接口 | 原生应用能使用事件、完成队列和进程树资源管理等能力 | 可以执行任意 Windows exe |
| PE64/Win32 子集 | 一个明确的 Windows 可执行格式和 API 子集得到支持 | Windows 内核驱动、所有桌面软件、.NET 与游戏均支持 |
| Ubuntu 虚拟机里浏览网页 | Cinux 能承载一个有浏览器的虚拟机 | Chromium 已经移植到 Cinux |
| 原生 Chromium | 浏览器及子进程直接运行在 Cinux 上 | 浏览器一定是在 Cinux 内编出来的，或安全隔离已经合格 |

## 当前代码透露的共同前置

这些是与新目标直接有关的容量和接口约束，不是发版流程清单。

| 当前事实 | 对新目标的影响 | 源码依据 |
|---|---|---|
| ext2 固定 1 KiB 块，只实现直接、一级和二级间接块 | 普通文件可寻址数据范围为 `(12 + 256 + 256²) × 1024 = 67,383,296` 字节，约 64.26 MiB。虚拟机磁盘、大型二进制和链接产物不能继续按这个范围设计 | [布局常量](<kernel/fs/ext2/ext2_layout.hpp#L41-L74>)、[文件块映射](<kernel/fs/ext2/ext2_layout.cpp#L97-L128>)、[读块处理](<kernel/fs/ext2/ext2.cpp#L457-L494>) |
| 最多 256 个块组，每组最多 8192 个 1 KiB 块 | 仅按这些上限推导，卷的数据寻址规模约 2 GiB。它不是完整容量实测，其他限制也可能先触发 | [挂载检查](<kernel/fs/ext2/ext2.cpp#L82-L96>)、[块组上限](<kernel/fs/ext2/ext2_layout.hpp#L53-L74>) |
| 日常启动默认 1024 MiB RAM，物理页管理编译期覆盖到 16 GiB 地址 | Ubuntu 虚拟机先用较小配置实测。Chromium 大构建需要验证并扩大内存覆盖，不能只改 QEMU 的内存参数 | [启动配置](<cmake/qemu/devices.cmake#L117-L123>)、[物理页覆盖](<kernel/mm/pmm_config.hpp#L41-L55>) |
| 标准化后的内部路径仍限制为 256 字节 | Chromium 源码、生成目录和软件包安装会遇到更深的路径，必须端到端测试 | [路径容量](<kernel/fs/fs_config.hpp#L35-L48>) |
| 匿名共享 mmap 被拒绝，文件共享映射依赖后端提供物理页 | 对大应用的共享内存、跨进程数据交换和图形缓冲构成明显限制 | [映射实现](<kernel/syscall/sys_mmap.cpp#L25-L48>)、[匿名映射分支](<kernel/syscall/sys_mmap.cpp#L78-L87>) |
| sendmsg 拒绝非空控制消息 | AF_UNIX 和 sendmsg 存在，仍不代表可以传递文件描述符。Wine、Wayland 及浏览器的部分进程通信需要继续补接口 | [发送实现](<kernel/syscall/sys_socket.cpp#L560-L571>) |
| x86_64 信号交付采用自有上下文，只设置处理器的信号编号参数 | Wine 等应用需要的完整 siginfo/ucontext 和备用信号栈应单独验证，不能用简单信号测试外推 | [信号交付](<kernel/proc/signals.cpp#L158-L194>) |
| epoll 的边沿触发只覆盖有限情况，单次通知模式被拒绝 | 必须验证 GLib、QEMU 和浏览器实际使用的事件循环，不能按接口名字认定兼容 | [事件注册检查](<kernel/syscall/sys_epoll.cpp#L288-L302>) |
| GCC、CMake 等原生开发工具部分来自构建电脑的 glibc 环境 | 已有自编译基础，但新增程序需要固定工具链、依赖和运行库版本，不能依赖每台开发电脑恰好装了什么 | [工具打包](<user/rootfs/buildroot/prepare.py#L96-L145>)、[开发环境说明](<user/rootfs/buildroot/README.md#L75-L95>) |

## 如何读投入估算

下表的人周是研究后的初步工程判断，不是上游给出的数字，也没有经过团队历史效率校准。假设一位熟悉 Cinux 和 C/C++ 的开发者全职投入，含定向测试与一次集成，不含所有硬件适配和长期维护。

每一项只估达到本项验收所需的增量投入，已列出的前置工作不重复计算。存在回退、依赖版本变化和深层并发问题时，实际投入可能超过区间。两人工作也不能简单把周期除以二。尚未确定方案的长期研究项不写确定完工日期。

## 主线排期清单

| 里程碑 | 前置 | 难度与增量粗估 | 必须能演示的成果 |
|---|---|---|---|
| 大应用开发环境 | 当前基线 | 中到高，4–8 人周 | 选定可写大容量后端或共享开发卷，超过 4 GiB 的文件读写及重启校验通过，深路径可用，8 GiB 内存配置完成有界并行构建测试 |
| Cinux 内编译 Linux | 大应用开发环境 | 中，2–6 人周 | 固定 Linux LTS 版本与配置，在 Cinux 完成串行和有限并行编译，得到内核及符号产物。先在外部参考 QEMU 启动到用户态，再验收增量重编 |
| 原生 QEMU 最小版本 | 大应用开发环境，可与 Linux 构建并行 | 高，6–20 人周 | 在 Cinux 执行 QEMU，使用单线程 TCG、单虚拟 CPU、串口和最少设备，先运行一个自测客体，再启动 Linux 与 BusyBox 到 shell |
| 完成 Cinux → QEMU → 自编译 Linux | 前两项均通过 | 中，1–3 人周 | 用 Cinux 编出的内核由 Cinux 内的 QEMU 启动，客体执行固定测试后退出，连续多轮运行不需要重启外层 Cinux |
| Ubuntu 命令行虚拟机 | 原生 QEMU、可用的大文件后端 | 高，3–6 人周 | 固定 Ubuntu amd64 cloud image，通过串口登录，能写盘、同步关机和重启。随后补用户态网络，完成 DNS、HTTPS 和一组固定软件包安装 |
| Ubuntu 桌面与浏览器虚拟机 | Ubuntu 命令行、图形或 VNC 输入输出通道 | 高，3–8 人周 | 先在受控机器上通过 VNC 看见桌面和操作浏览器，再接 Cinux 原生 VNC 客户端或 QEMU 显示后端。浏览器访问固定测试网页，记录交互延迟、CPU 和 RAM |
| Cinux 内运行 Ubuntu 程序子集 | 大应用环境、共享内存与进程通信工作 | 很高，8–20 人周 | 选定发行版和一组未经 Cinux 专改的 amd64 程序，依次跑通 shell、coreutils、Git、curl、Python、SQLite，再完成受控包集合的解包、配置和升级 |
| 原生 Chromium 无界面渲染 | Linux 用户态基础、浏览器所需内存与 IPC | 研究项目，12–24+ 人周 | 先在支持的 Linux 构建机产生目标产物，在 Cinux 原生运行 content_shell 或选定 headless 目标，执行 JavaScript、输出 DOM 或截图。早期只用本地可信页面 |
| 原生 Chromium 图形与安全上网 | 无界面渲染、独立窗口协议、权限与沙箱 | 很高，另计 12–24+ 人周 | 接入 Wayland 或专用 Ozone 后端，软件渲染即可起步。验证多进程、中文输入、下载和持久用户资料。必须启用并验证隔离后才能把公网浏览作为正式验收 |
| 在 Cinux 本地编译 Chromium | 原生工具链可运行、足够的文件与内存规模 | 很高，另计 8–20+ 人周 | 固定源码、工具链与 GN 配置，在 Cinux 内实际执行全量和增量编译及最终链接，运行生成的目标。不能把编译搬到远端后称作本地自举 |

### 大应用开发环境

- [ ] 固定第一阶段只服务 x86_64。ARMv7 保留现有开发板路线，后续可以作为 QEMU 客体，不要求在小板上构建 Chromium。
- [ ] 首轮选择一条最短的可写大文件通路，并记录取舍。扩展 ext2、实现 9p 等共享开发卷、直接承接 ext4 都是候选，不要求先完成全部 ext4 日志功能。
- [ ] 先按 32–64 GiB 开发卷规划 Linux 和 QEMU，再为 Chromium 准备至少 200 GiB 的独立构建空间。后者是本项目预留量，不是官方最低值。
- [ ] 测试 4–8 GiB RAM 的正常运行和资源不足行为。Chromium 构建前再把 16 GiB 物理地址覆盖限制改为可配置或动态管理，并验证超过 16 GiB 的物理内存。
- [ ] 验证长路径、大目录、重命名、链接、文件时间戳和并行读写。按实际构建程序补缺口。
- [ ] 固定 libc、GCC/binutils、Python 和基础工具清单，给每个目标保存版本、配置、补丁和测试结果。

### 在 Cinux 内编译 Linux

- [ ] 从仍在维护的 LTS 线选一个固定修订，建议先评估 Linux 6.12.y 或 6.18.y，开工时记录归档哈希和配置。
- [ ] 准备 GNU Make、Bash、GCC、binutils、Flex、Bison、Perl、bc，以及选定配置需要的开发库。现有默认 shell 是 BusyBox ash，不能直接当作 Bash 替代。BTF、签名、Rust、objtool 等按配置审查，不能把某一版的可选依赖一律当必选，也不能全关功能掩盖目标所需依赖。
- [ ] 先做含串口、ELF 执行、initramfs 和最小用户态所需选项的小内核，再扩到有网络和磁盘的配置。Ubuntu 虚拟机先用发行版自带内核，不强求最小内核直接承担 Ubuntu。
- [ ] 用 `make -j1` 定位最初错误，再逐步增加作业数。保留全量、无改动重建和改源后重建的日志。
- [ ] 验收物是确实能启动到用户态的内核，不能只检查文件存在或 make 退出码。

### 在 Cinux 内运行 QEMU

- [ ] 目标是 `qemu-system-x86_64`，不是在 Linux 上模拟别的 Linux 程序的 QEMU linux-user。
- [ ] 第一版选 TCG，即把客体指令翻译成本机可执行代码的软件模拟。先用 `-accel tcg,thread=single` 与单客体 CPU，不依赖 Cinux 硬件虚拟化。它只限制客体 CPU 执行方式，不代表 QEMU 的主循环、辅助任务和块 I/O 不再使用线程。
- [ ] 可先在 Linux 侧交叉构建 Cinux 能运行的版本，验证运行时，再把 QEMU 自身的构建搬到 Cinux。Linux 上编出的通用二进制能否直接用，要实测。
- [ ] 固定 QEMU、GLib、构建工具和必要依赖。第一版不带 GTK、SDL、音频、OpenGL、USB 重定向、qcow2 和用户态网络，随后按里程碑逐项接回。库依赖和配置开关以固定版本的构建检测为准。
- [ ] 分别验证 JIT 可执行映射与权限切换、pthread/futex、计时和信号上下文、事件循环、文件定位读写、截断。某些系统调用可能有回退路径，需探针确认后才能列为硬前置。
- [ ] 最小 Linux 客体可通过 QEMU 的直接 Linux 启动入口加载，不要求先给 Cinux 做 UEFI、VirtIO 驱动或硬件虚拟化。
- [ ] 大型虚拟磁盘放在 Cinux 可寻址的大文件后端。客体磁盘里面是 ext4，不代表 Cinux 自己会解析 ext4，外层只需正确保存磁盘文件。

### Ubuntu 与浏览器的虚拟机路径

- [ ] 固定一个 Ubuntu LTS amd64 cloud image，记录校验值。当前 Noble 官方索引中的磁盘镜像标为 QCow2、UEFI/GPT，不能按扩展名假设是 raw，也不能假设早期 BIOS 配置能直接启动。首版可在开发电脑预先转成 raw，并准备匹配固件或明确的直接内核启动方案。用受控的 cloud-init seed 配置开发账户或 SSH 公钥，凭据不写入公共镜像。
- [ ] 从单虚拟 CPU 和小内存配置起测，选择客体内核已经支持的虚拟磁盘控制器。先有串口和持久化，再做网络与显示。
- [ ] 接入 libslirp 等 QEMU 用户态网络后，测试 UDP、TCP、DNS、TLS 及连接数。Cinux 当前网络并发上限和行为也要对应扩展。
- [ ] 桌面早期可以通过只监听可信网络的 VNC 服务连接到外部查看器。不要等 Cinux 完成 GPU 驱动才显示客体桌面。
- [ ] 浏览器优先验证预构建产物。Ubuntu 的 Chromium 分发可能走 Snap，不能把一个包管理命令当成整个安装路径已经支持。固定来源、安装方式和版本。
- [ ] 浏览器运行在 Ubuntu/Linux 内时，使用客体已有的沙箱，测证书校验、脚本执行、下载和资料目录持久化。
- [ ] 纯软件模拟的“成功启动”和“适合每天上网”分开报告。需要记录启动时间、输入延迟和页面加载时间，不在未测之前承诺流畅。
- [ ] 在 Ubuntu 虚拟机内编译 Chromium 可作为额外实验，但性能成本可能很高，先不把它放在第一批浏览器体验之前。

## 可以并行开的系统功能

### 独立图形应用与桌面体验

这项服务于 Ubuntu 虚拟机显示、原生浏览器和 Windows 图形程序，不要求绑定其中某一条兼容路线。

- [ ] 定义进程外窗口和共享缓冲协议，验证两个独立应用同时显示、调整大小、切换焦点，其中一个崩溃不终止桌面。不能只在桌面进程里再创建一个控件就算多应用。
- [ ] 接入统一剪贴板、文件选择、中文输入、拖放和窗口吸附。先做文本复制和基础输入，再扩到文件及复杂输入法。
- [ ] 做原生 VNC 查看器，把已经运行的 Ubuntu 桌面显示在 Cinux 窗口里，支持键鼠、大小调整和断线重连。
- [ ] 做任务管理器，展示应用、子进程、CPU 和内存实际用量，联动进程树管理实现“一次结束整个应用”。先补可信计账，不用常量或占位统计显示成功。
- [ ] 浏览器声音、音量管理、摄像头、GPU 和视频硬解分别后排。网页能显示不代表这些多媒体功能已经支持。

独立窗口协议建议先做 1–2 人周原型验证，再按所选 Wayland/Ozone/Vesper 方案估量。桌面功能可拆开交付，不作为 Linux 命令行或 QEMU 串口的前置。

### ext4 与大容量存储

难度高，建议为选定特性集的可写、带恢复能力的数据盘预留 10–24+ 人周，具体取决于复用还是自行实现。它与 Linux/QEMU 主线并行，第一阶段的大文件通路不必等待全部完成。

- [ ] 定义支持的块大小、extent 树、目录索引、校验和及其他磁盘特性组合，未知不兼容位明确拒绝。
- [ ] 先只读挂载干净卸载的卷。需要日志恢复的卷在没有安全重放能力时拒绝挂载，不能为了只读验收误写用户盘。
- [ ] 再做写入、分配回收、目录操作、同步写与错误处理，接入并验证 JBD2 提交和日志重放。
- [ ] 用 Linux 创建的测试卷双向读写，超过 4 GiB 的单文件、超过 2 GiB 的卷和异常停机恢复均纳入测试。
- [ ] 根盘迁移在数据盘验收以后，不把“识别 ext4 超级块”当作完整支持。

日志保护的主要是文件系统更新的一致性，应用内容是否已持久化仍需正确的同步写和设备刷新语义。具体依据见 [ext4 日志文档](https://docs.kernel.org/filesystems/ext4/journal.html)。

### Windows 好用功能：Cinux 原生接口

建议与 Linux/QEMU 主线并行，先做事件等待和进程树管理，再做异步 I/O。该项不依赖 PE 装载器，也不要求先移植 Wine。

- [ ] 事件对象与等待组合，先支持手动复位和自动复位、wait-any、超时，以及等待期间对象被关闭。验证并发无丢失唤醒。wait-all 的原子状态消费后加，不用循环逐个等待冒充。初步预算 2–4 人周。
- [ ] Job 风格的进程树管理，即对整个应用及其子进程设置资源限制、统计用量和统一结束。必须处理 fork/exec 之后的继承，不能只向某个进程组发信号。初步预算 3–6 人周。
- [ ] IOCP 风格的完成队列，即异步读写完成后把结果投给工作线程。定义部分传输、取消、关闭、缓冲区寿命和错误通知，先文件与套接字。第一版可用有界线程池执行阻塞 I/O，但标明这是原型后端，不是完整的驱动异步路径。验证每个已接受请求恰有一个终态，取消竞争不重复释放。epoll 的就绪通知不能直接当完成结果。初步预算 4–8 人周。
- [ ] 用异步文件复制、多客户端服务和“关闭应用时结束全部子进程”验收。Windows 风格接口与 Linux 二进制接口可共享内核实现，不需要复制整个 NT 内核。

- [ ] 原生服务管理支持开机启动、查询状态、停止和崩溃退避重启，用一个网络服务验收。先用 Cinux 自己的配置，不承诺兼容 Windows 服务程序。
- [ ] 原生软件安装记录文件归属，支持升级失败恢复旧版本和安全卸载。回滚需要文件写入、重命名与同步语义共同保证，不等于兼容 MSI。

这些预算分别对应前三类内核功能，不含全部服务与安装器。不能把整项宣称为一次两周迭代。注册表、服务界面、安装器等作为后续用户态产品，不作为第一版异步 I/O 的前置。

### Windows 程序兼容：独立分支

该分支与 Windows 实用接口分开验收。先固定 x86_64，不承诺任意 exe，也不承诺 Windows 驱动、.NET、DirectX 或带反作弊软件。

- [ ] 在已经运行的 Ubuntu 虚拟机中安装固定版本 Wine，运行一组自有许可的 Windows 测试程序。它较早证明“Cinux 上能使用 Windows 应用”的体验，但必须标注应用实际运行在 Ubuntu 内。
- [ ] 需要更完整 Windows 环境时，也可由 Cinux 内的 QEMU 直接启动合法的 Windows 镜像，不必再套进 Ubuntu。按所选版本单列固件、TPM、RAM、虚拟设备及授权要求，TCG 性能另测，不承诺任意软件。
- [ ] 选择原生路线。长期希望运行真实 Windows 软件时优先评估移植 Wine，纯 PE64/Win32 小子集则适合教学与自主接口实验，不建议同时维护两套扩大范围的兼容层。
- [ ] 自行实现 PE64 小子集时，明确映像装载、基址重定位、导入解析、x64 调用约定、TLS 和异常处理范围。第一样本只导入 `GetStdHandle`、`WriteFile`、`ExitProcess`，不用 C 运行库，测试输出、退出码、畸形映像拒绝及非首选基址装载。以后再扩到固定的开源工具。探索预算 4–8 人周，只对应明确的小子集。Wine 已有自己的 PE 装载器，这个实验不是移植 Wine 的前置。
- [ ] 原生 Wine 先做无图形的控制台程序与 wineserver。重点检查文件描述符传递、共享映射、信号备用栈、siginfo/ucontext、进程同步和虚拟地址布局。按版本确认要求，不能只因为已有 signal/clone/futex 就认定能运行。
- [ ] Wine 图形程序需要独立的显示、输入、字体和剪贴板后端。第一目标可以是固定的 Win32 文本编辑器，不以任意 Windows 桌面程序为承诺。
- [ ] 原生 Wine 控制台与 GUI 分别作为研究项目，每项先做 1–2 人周兼容性探针再重估，不在本轮给出确定完工日期。考虑采用新 WoW64 路线时，32 位 Linux 运行库并非所有配置的硬前置。
- [ ] 复用 Wine 时核对 LGPL 条款及第三方组件要求，保持它的代码和许可证可识别。不要将兼容层整体宣称为 Cinux 的 MIT 代码，也不分发无权分发的 Windows 系统文件。

## Chromium 原生路线的额外前置

### 构建、运行和显示要分别验收

上游 Linux 构建文档要求 x86-64、至少 8 GB RAM、强烈建议超过 16 GB，并要求至少 100 GB 可用磁盘。官方支持的 C++ 工具链是 Clang 与 libc++，现有 GCC 自编译成绩不能直接替代这些依赖。版本和构建执行器以开工时固定的 Chromium 修订为准。

本项目建议把长期构建机器配置预留到 32 GiB RAM、200 GiB 以上可用空间，并为系统自身和并行链接保留余量。这是项目建议，不代表当前 Cinux 已经能管理这些资源。

- [ ] Linux 侧先做参考构建，固定依赖、GN 生成工具和该修订要求的实际构建执行器，再在 Cinux 验证相同产物。
- [ ] 将 V8 与基础线程/内存测试作为诊断工具，但不把它们当作 Chromium 已经运行。
- [ ] 先实现本地可信页面的渲染、JavaScript、DOM 或截图。headless 仍有进程、线程、共享内存和平台依赖，不能理解为一个不需要系统支持的命令行程序。
- [ ] 浏览器 IPC 需要依据所选版本验证共享映射、描述符传递、文件与计时器行为，缺口由应用探针逐项确认。
- [ ] 图形路径优先选一条：通过 Wayland 协议接入 Vesper 的合成器，或实现专用 Ozone 平台后端。Ozone 是 Chromium 连接底层窗口、输入和图形表面的接口。默认桌面已有窗口，不代表它已经支持外部 Chromium 窗口。
- [ ] 第一版允许软件渲染，GPU 加速与视频硬解后排，不把 GPU 驱动成熟作为显示网页的必要条件。
- [ ] 浏览器完整运行与在 Cinux 内编译它分别记录。交叉构建运行通过后，再搬入 Clang、链接器、libc++、Python、Git 和完整构建执行器，做本地全量、增量及最终链接。

### 公网上网必须有隔离

Chromium 的 Linux 沙箱使用命名空间、权限隔离和 seccomp-BPF 等机制。Cinux 可以实现相容接口，也可以为新平台设计经验证的替代，但不能用空实现返回成功来宣称安全。

- [ ] 首次原生渲染只访问自带的离线页面和受控测试服务。
- [ ] 调试时如果临时关闭沙箱，结果标为实验，不放真实用户资料，不把打开公网网页作为通过条件。
- [ ] 正式上网之前，验证渲染进程不能读取测试私有文件、不能越权操控其他进程，资源超限和渲染器崩溃不会终止桌面，HTTPS 证书校验有效。
- [ ] 制定后续版本更新路径。长期停在一个旧浏览器版本会累积已知安全问题，不把一次移植成功当作永久完成。

## 硬件虚拟化的独立研究项

TCG 首次启动 Ubuntu 不依赖这个项目。若希望 Ubuntu 桌面和浏览器真正适合日常使用，再开硬件加速后端。

- [ ] 先选 Intel VMX 或 AMD SVM 一条路线，检查开发设备是否暴露虚拟化能力。
- [ ] 实现客体 CPU 状态、内存二级转换、中断与定时器等核心路径，并给 QEMU 接一个清晰的加速接口。
- [ ] 可以评估兼容部分 KVM 设备接口，也可以做 Cinux 专用后端。创建一个名为 `/dev/kvm` 的节点远远不够。
- [ ] 外层如果仍由 QEMU 启动 Cinux，必须另验嵌套虚拟化是否可用，不能借用开发电脑的 KVM 能力当成 Cinux 已有实现。
- [ ] 初期按一个虚拟 CPU、一个已知 Linux 客体验收，再测 Ubuntu 和多虚拟 CPU。这是独立的月级以上研究项目，第一轮原型完成后再估剩余投入。

## 首轮执行建议

以两个星期为一次评审周期，不把尚未探索的大移植硬塞进同一个周期。团队人数和可投入时间未定，因此暂不写公历交付日期。

第一轮的交付如下：

- [ ] 固定 Linux LTS、QEMU、Ubuntu 镜像和 Chromium 参考修订，记录哈希及许可证。
- [ ] 选定可写大文件通路，完成跨 64 MiB 边界探针，随后验证超过 4 GiB 的文件与多 GiB 开发卷。
- [ ] 生成 Linux 构建的依赖包与配置，在 Cinux 跑到第一次实际的构建失败并分类。时间允许时完成最小内核产物。
- [ ] 形成 QEMU/GLib 最小依赖包，先验证线程、JIT 映射、事件循环和定位 I/O，不以只打印版本号当成运行成功。
- [ ] 输出当前实际阻塞、修复范围和下一轮估算。缺工具、缺接口、并发错误和容量不足分别登记，避免无限扩成补全全部 Linux 系统调用。

如果有两位主开发，建议一位负责存储/内存与兼容接口，另一位负责 Linux/QEMU 应用移植与验收。Windows 实用接口和图形后端有独立负责人时并行，无人负责就明确排在主线之后。

完成 Ubuntu 浏览器演示后，再依据实测性能选择先做硬件虚拟化还是原生浏览器。原生 Ubuntu 用户态子集、Wine 与 Chromium 会共享大量内核工作，但不能把几个项目的调试和测试视为免费。

## 调研资料

以下资料于 2026-11-25 查阅。上游 main/master 和文档会变化，正式开工应固定源码修订，不依赖长期不变的最低版本数字。

- [Linux 6.12 构建环境要求](https://www.kernel.org/doc/html/v6.12/process/changes.html)
- [Linux Kconfig 最小配置机制](https://www.kernel.org/doc/html/v6.12/kbuild/kconfig.html)
- [Linux 长期维护版本](https://www.kernel.org/category/releases.html)
- [QEMU 系统模拟与加速器](https://qemu-project.gitlab.io/qemu/system/introduction.html)
- [QEMU 单线程与多线程 TCG](https://qemu-project.gitlab.io/qemu/devel/multi-thread-tcg.html)
- [QEMU 直接 Linux 启动](https://qemu-project.gitlab.io/qemu/system/linuxboot.html)
- [QEMU 构建环境](https://qemu-project.gitlab.io/qemu/devel/build-environment.html)
- [QEMU 构建系统](https://qemu-project.gitlab.io/qemu/devel/build-system.html)
- [Ubuntu Noble 官方云镜像索引](https://cloud-images.ubuntu.com/noble/current/)
- [cloud-init 的 QEMU 启动示例](https://docs.cloud-init.io/en/26.1/howto/launch_qemu.html)
- [KVM 实际内核接口](https://docs.kernel.org/virt/kvm/api.html)
- [Microsoft PE/COFF 格式](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
- [Microsoft x64 调用约定](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170)
- [Windows 等待语义](https://learn.microsoft.com/en-us/windows/win32/sync/wait-functions)
- [Windows 完成端口](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports)
- [Windows Job 进程树管理](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)
- [Wine 架构资料](https://gitlab.winehq.org/wine/wine/-/wikis/Wine-Developer's-Guide/Architecture-Overview.md)
- [Wine 构建与新 WoW64 路线](https://gitlab.winehq.org/wine/wine/-/wikis/Building-Wine.md)
- [Wine 进程间通信实现](https://gitlab.winehq.org/wine/wine/-/raw/master/dlls/ntdll/unix/server.c)
- [Wine x64 信号与异常实现](https://gitlab.winehq.org/wine/wine/-/raw/master/dlls/ntdll/unix/signal_x86_64.c)
- [Wine 许可证声明](https://gitlab.winehq.org/wine/wine/-/raw/master/LICENSE)
- [Chromium Linux 构建要求](https://raw.githubusercontent.com/chromium/chromium/main/docs/linux/build_instructions.md)
- [Chromium Ozone 平台接口](https://raw.githubusercontent.com/chromium/chromium/main/docs/ozone_overview.md)
- [Chromium Linux 沙箱](https://chromium.googlesource.com/chromium/src/+/HEAD/sandbox/linux/README.md)
- [Chrome headless 模式说明](https://developer.chrome.com/docs/automation-and-testing/headless)
- [ext4 高层设计](https://docs.kernel.org/filesystems/ext4/overview.html)
- [ext4 日志与恢复](https://docs.kernel.org/filesystems/ext4/journal.html)
