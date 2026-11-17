# ARMv7 buildroot desk：输入诊断与独占抓取修复

## 断点判定

本轮在原工作区上增加有限次数的诊断（每个 host 的提交/完成各最多 16 次、
状态轮询 12 次、平板解码 16 次、pointer 入队 48 次），先跑原版
`scripts/check-armv7-desk.sh`。尚未修改 EHCI、IRQ 或输入行为时，串口已经记录：

```text
[usb-diag] ehci 2184200 periodic cmd=11 sts=4005 intr=4 list=803FA000
[usb-diag] ehci 2184200 irq-status (polled) sts=400D intr=4 frame=3570 token=88180
[usb-diag] ehci 2184200 complete 2 token=28100
[usb-diag] tablet report 2 status=0 length=6
[usb-diag] tablet decoded buttons=1 x=2047 y=7509 stream=80079248
[usb-diag] pointer push stream=80079248 type=1 code=272 value=1
```

`USBSTS` 的周期调度运行位已置位，qTD 的 Active 位也已清除，左键报告成功
进入同一个 pointer EventStream。两台 host 均由 EventStream 的 poll hook 调用
`pump()` 收割完成传输；本端口当前没有启用或分派 USB 的 GIC IRQ。
这里的 irq-status 是读取状态寄存器的轮询诊断，不是新增的 USB IRQ handler。
因此没有发现 host1 周期调度、IRQ 或平板解码的断点，最终没有保留 EHCI 改动。

`svc.cpp` 原来的诊断只打印负返回值。非阻塞输入读完后继续出现 EAGAIN，
不能证明此前没有成功读取事件。

## 两处实际问题

1. buildroot 的 `/etc/init.d/desktop` 通过 `su -` 启动桌面，却没有在新的登录
   环境中设置 `VESPER_FBDEV_TRACE=1`。desk 的 `trace_event()` 因此直接返回，
   点击与 pty 的串口判据均假阴性。只补这一处后，原脚本已经能看到两次
   `pdown 1 40 110` 和 `pty=1`，但拖动、关闭的像素判据仍失败。
2. UART 与 USB 共享 event0，但 UART 的 `SerialTryGetChar()` 在向 event0
   发布按键后，仍把同一个字节交给 console tty，即使 desk 已 EVIOCGRAB。
   原脚本的串口命令同时在 console shell 和桌面 pty shell 中执行。USB
   键盘的 glyph 路径原已遵守 grab，因此 USB 键盘对照测试能通过。
   补上 UART 的 grab 检查后，原脚本的命令输出、拖动和关闭像素判据均恢复。

另用 QMP stdio / UART FIFO 探针做了对照：USB 键盘输入同一条管道命令后，
拖动会改变帧缓冲；旧 UART 路径输入后，拖动前后的原始帧缓冲与截图均相同。
对照直接读取了 LCDIF 指向的物理帧，排除了仅为 QMP 截图缓存的解释。

## 最终改动

- `desktop`：在 `su -` 的命令内部 export 桌面 trace 开关。
- `EventStream::grabbed()`：在 IRQ guard 和流锁保护下查询独占拥有者。
- ARMv7 `platform/serial.hpp`：UART 字节始终发布到 event0；流被抓取时继续
  排空 UART，不再把字节交给 console tty；释放抓取后仍能正常读取串口。
- 扩展现有 input 单元测试，覆盖抓取、外来拥有者释放、最终释放后的查询结果。

临时诊断已移除。未改动先前的 fault、AP=6、abort 汇编、装载器尾页、clone
参数/NULL 座位、exec 初始 flags、信号投递或 prepare.py 的 armv7 卷夹具。

## 证据位置

本轮完整记录保存在 `build-armv7/usb-desk-evidence/`：

- `desk-before.serial.log` / `desk-before.check.log`：仅增加诊断的原始失败。
- `diagnostics.patch`：当时使用的全部有限次数诊断。
- `desk-trace.serial.log` / `desk-trace.check.log`：仅启用用户态 trace 的对照。
- `desk-grab.serial.log` / `desk-grab.check.log`：修复抓取后直接执行原脚本，退出 0。
- `desk-final.serial.log` / `ctest-final.log`：移除临时诊断后的最终回归。
- `fix.patch`：本轮五个实现/测试文件的完整 diff。
- `protected-sha256.json`：九个关键已有修复文件在本轮开始时的哈希；修复后逐个核对一致。

原脚本修复后的像素见证：

```text
click reached the icon: present
shell answered through the pty: present
terminal box: 46,81 588x399
terminal ink pixels: 2861 (need 40)
the window moved with the drag: present (left 46)
a second window beside the terminal: present
the closed window is gone: present
the terminal survived the close: present
```

## 构建与回归

内核构建使用：

```sh
PATH="/usr/sbin:$PATH" cmake --build build-armv7 --target arm_image -j$(nproc)
```

`buildroot_volume --ensure` 会保留已有卡；本轮用 `build.py` 的完整构建刷新
overlay，确保卡上的 desktop 脚本也包含修复，而不是仅修改宿主源码。

最终内核移除临时诊断后的串口：

```text
trace[1] poll=1 used=0 kbd=0 ptr=0 pty=1
trace[36] pdown 1 40 110
trace[42] pdown 1 40 110
```

最终运行 `ctest --test-dir build-armv7 -j1 -R '^(arm_boot|arm_desk|arm_gui_keyboard)$'
--output-on-failure`，三项全部通过（退出 0）：

| 测试 | 结果 | 耗时 |
| --- | --- | --- |
| arm_boot | Passed | 55.02 s |
| arm_desk（直接调用原 bash 脚本） | Passed | 74.48 s |
| arm_gui_keyboard | Passed | 16.09 s |

合计 145.59 s；`ctest-final-detail.log` 保存完整输出，`desk-final.check.log`
提取桌面脚本全部串口/像素判据。输入单元测试另由
`ctest --test-dir build/test -R '^input$' --output-on-failure` 验证通过（0.01 s）。
