# armv7 车道并入 buildroot 主线：现状与遗留

状态：核心链路已通（2026-11-16）。buildroot 卡引导至根 shell，车站核心测试
全绿；desk 的 pty 终端 spawn 仍失败，诊断仪器在树，本文记录全链证据与复现。

## 已达成的统一

- buildroot 模块双车道加载（TERMINAL 返回拆除）：armv7 车道同样获得
  buildroot_volume / run / run_gui 目标。
- armv7 defconfig（Bootlin armv7-eabihf glibc，GCC 14.3，buildroot 内置）；
  ARM 卡封壳改为 2GiB MBR+ext2（QEMU SD 仅收 2 的幂）。
- 内核翻根、busybox init 从卷上起：sysinit → ::once desktop → respawn su →
  `~ #` 根 shell，全程走卷。
- GUI 变体 dtb（qemu-init-gui.dts）：init-env 只带 CINUX_SESSION=gui（双串
  只读首串，第二串不生效），run_gui 用它点灯桌面脚本。

## 本会话修复的内核 bug（四件，全部双车道验证）

1. 装载器部分尾页零填：LOAD 段文件部分不按页结尾时，最后一页的已初始化数据
   （glibc stdio FILE 结构）被匿名零填吞掉。修复：座位时就地填充（文件字节
   +零尾+按段权限映射）。证据：崩溃现场 stdio 结构读出正确值。
2. setitimer 的 ARM32 ABI：内核 Itimerval 用 64 位字段共 32 字节，回写时多
   16 字节盖掉 glibc wrapper 栈帧的保存寄存器。修复：struct 改 int32 四字段
   共 16 字节。证据：故障模式消失，双车道绿。
3. vectors.S 缺 SVC 模式数据中止路由：_vec4 对非用户态中止直接停机，内核态
   对用户页的裸访问中止（syscall 处理器经裸用户 VA 读用户内存）无法到达需求
   填充。修复：新增 _abort_svc_save 保存路径（含被中断 SVC 的 lr），交
   c_abort_trap。codex 定位、人工复核汇编。
4. EntryIsUser 补 AP=6：只读页编码改为双世界只读后，uaccess 的用户可达判定
   需接受 AP=6，否则只读页上的所有用户串读出 EFAULT。

## 遗留问题：desk 的 pty 终端 spawn

现象：vesper-fbdev 打开 fb 640x480 与 event0/event1 后，
`PtySession::open("/bin/sh")` 内部的 clone 返回 EFAULT，desk 打印
"cannot spawn /bin/sh under a pty: Bad address" 后仍进主循环（fb/输入工作，
仅 pty 终端缺失）。

证据链：
- 内核裸读 clone 第 5 参栈槽（[user_rsp]）得 0，即 child_tidptr = NULL。
- glibc fork 本应传 &pd->tid（TLS 块内，非零）。TPIDRURO 已确认非零
  （0x40188500，落在 PT_TLS 所在 LOAD 段内——静态 TLS 在镜像段内是合法的）。
- 怀疑方向：(a) glibc arm clone.S 的第 5 参栈槽安排与内核读取位置的错位；
  (b) 栈页曾被 COW 修复的 kFile 重填清零（修复走拷贝应保数据，需验证修复时
  源帧内容）；(c) clone 旗标位型的工具链差异（Bootlin 头文件的 _IOC/CLONE
  常量 vs 内核常量）。
- 已排除：real_timer 武装（不武装也崩）、COW 账本泄漏（释放清单跟踪为空）、
  L2 表共享（map_user 每空间自有表）、NULL 座位容忍修复后仍失败。

复现：buildroot_volume 构建后，GUI 变体 dtb 引导（见 run_gui），串口 20 秒后
观察 vesper 的 spawn 报错。诊断仪器：faults.cpp 的取指死亡转储（PID1 生命
周期环）、svc.cpp 的 EFAULT 捕获器（pid+参数）、HandleArmClone 的裸读打印。

## 车站退役清单

- busybox：buildroot 卷自带 ✓（车站的 build-armv7-busybox.sh 退役待删）
- glibc hello：探针夹具，保留（build-armv7-glibc-prog.sh）
- desk：交叉构建应并入构建图（当前 build-armv7-vesper-desk.sh 保留，接入点
  为 prepare.py 的 armv7 overlay 拷贝，已预留 /bin/vesper）
- diskprog/probe：测试夹具，保留
- create_armv7_sd.sh：制卡封壳逻辑已并入 build.py，车站卡仅供 arm_boot 夹具
  场景，退役待 arm_boot 换卷后

## 加固项（GPT 会诊发现，未修）

- 内核拷出对用户 RO 页走"特权 RW"位，静默绕过 COW（AP 编码已改双世界只读，
  旁路已封；数据中止门的用户窗口双模式路由已加——两者配套，注意回滚其一即
  失效）
- TLBI 前缺 DSB、缺编译器屏障，SwitchRoot 缺完成屏障（space_control.hpp 已
  补 InvalidatePage/InvalidateAll/RefreshRoot/SwitchRoot 的 dsb+isb+clobber；
  其余 TLBI 点位待审计）
- break-before-make：valid→valid 帧替换的时序审计未做

## 收尾更新（同日）

四个 ctest 判据全绿（独立复核）：arm_boot 55.0s、arm_init 55.2s、arm_desk 75.3s、
arm_gui_keyboard 16.7s。buildroot 卡 + GUI 变体引导后 desk 完整运行
（`vesper-fbdev: fb 640x480 ... running`），控制台根 shell 正常。

收尾阶段修掉的三个根因：

1. clone ABI：arm32 EABI 用 r0-r6 传参，child_tidptr 在 r4 而非用户栈。原
   HandleArmClone 读栈是 x86 形状的误移植，导致 forkpty 的 clone 恒报 tid 座位
   不可写。修复为 r8/r10 对调（child_tid 取 trap.r4，tls 取 trap.r3）。
2. exec 的 SPSR 继承：车站道 /glibc 以 Thumb 态运行，exec /desk 时旧 T 位被
   继承，ARM 镜像按 Thumb 解码跑到撞墙（exception 1）。exec 现按架构生成初始
   flags，Store 处理 Thumb 互操作（奇入口置 SPSR.T）。
3. 信号投递：中性 DeliverPendingSignals 在 armv7 的 serve 路径上接通过一次即
   打崩开机自检——投递仍用 x86 调用约定（压栈返回地址），ARM 需要用户 LR、trap
   未保存、bridge 未完整恢复寄存器。修复后补 ARM 信号帧与跳板
   （sigreturn=119 / rt_sigreturn=173 两套布局），ppoll 的临时掩码下投递、
   返回后恢复原掩码，EINTR 不再循环；新增 ARM 信号真探针。

遗留清理项：车站构建脚本仍在（busybox/glibc/desk 工件按夹具使用，探针与
diskprog 保留）；TLBIALL 兜底与 break-before-make 审计仍待做（加固项，不影响
现有判据）。arm_gui_keyboard 的 QMP 通道从 Unix socket 改为 stdio 管道（沙箱
可移植性，断言未变）。

## 测试迁 buildroot 卷（同日第二批）

四个 ctest 全部改为引导 buildroot 卡（native-build.ext2），车站卡与它的整套
支撑退役：create_armv7_sd.sh、build-armv7-busybox.sh、click/press/demo 三个
交互脚本删除；desk/glibc 探针夹具经 prepare.py 进 buildroot overlay（/motd
9B、/hello、/readme、/scratch、/etc/motd 99B、/etc/zh*、/prog、/glibc），
boot 车道的座位世界在 buildroot 卡上读同一批路径。

- 自举编译（内核在 guest 里编译自己）整体移除：kernel_selfhost_volume 目标、
  prepare-kernel-selfhost.py、build-kernel-in-guest.py、station34 kernel
  selfhost 测试、run-cell 的 selfhost 套件。Guest 类 QEMU 引导骨架抽到
  scripts/guest.py 共享。
- desk/glibc 探针进入构建图：armv7 车道定义 vesper_desk / arm_glibc_hello
  目标，buildroot overlay 的依赖挂钩自动认领；buildroot prepare.py 缺夹具
  直接报错而不是静默跳过。
- 交叉工具链正本：Bootlin GCC 15.2.1（/opt/arm-gnu-toolchain，支持 C++23
  <print>），工具链文件直连，.cross14 化装树概念废弃。desk 二进制 strip 后
  1.1MB。
- CINUX_BUILDROOT_VOLUME 在车道 cmake 作用域为空（根 CMakeLists 的加载顺序
  在车道之后），armv7 车道自己拼写卷路径（CINUX_ARM_BUILDROOT_VOLUME）。
- desk 输入两处修正：buildroot 卡桌面脚本补 VESPER_FBDEV_TRACE=1（事件
  trace 上串口，此前点击判据是假阴性）；UART 输入桥尊重 event0 的
  EVIOCGRAB——桌面独占键盘后，串口敲键不再同时落进控制台 shell
  （serial.hpp SerialTryGetChar + EventStream::grabbed）。
