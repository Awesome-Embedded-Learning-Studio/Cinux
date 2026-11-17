# ARMv7 desk：exec 必须重新建立指令状态

## 定位

同一 desk ELF 的入口是 `0x400155f0`（ARM）。在 HandleExecve 的中立
frame 写入位置打印入口和 flags，在 ARM bridge Store 后打印实际 SPSR：

| 修复前路径 | entry | neutral flags | 实际 SPSR |
| --- | --- | --- | --- |
| 车站 `/glibc` → `/desk` | 400155F0 | 202 | 70030 |
| buildroot `/bin/sh` → `/bin/vesper` | 400155F0 | 202 | 20000010 |

车站前一个 glibc ELF 入口为 `40010609`，是 Thumb。ARM frame bridge 原先
忽略 frame.r11，而且只对奇数入口置 T 位，没有在 exec ARM 入口时清 T。
因此 desk 在车站继承 Thumb 状态；buildroot 的 ARM busybox 则碰巧给出正确
状态。这解释了同一二进制、同一加载器在两个外围世界中的差异。

此前把 undefined 异常 LR 一律按 ARM 态减 4 并推断未对齐 ARM 取指，前提
并不成立：这次 desk 入口处实际 T=1。无需假设栈或文件页被污染。

## 改动

- ARM/x86 的 user_entry.hpp 提供 UserEntryFlags：ARM 从 ELF 入口得到
  用户模式 0x10 或 Thumb 用户模式 0x30；x86 保持 0x202。
- HandleExecve 用该架构值设置 frame.r11，不继承旧程序的条件码、IT/J/T。
- ARM frame bridge 把 frame.r11 写回 SPSR。普通调用由 Load 原样保存 SPSR，
  所以 Thumb syscall 的偶数返回 PC 不会导致 T 位丢失。
- 保留 `[exec]` 和 `[exec-arm]` 诊断；不改动此前验证的 fault、AP、abort
  汇编、装载器尾页、clone 参数及 NULL 座位修复。

## 实测证据

修复后的车站卡（相同普通 armv7-qemu.dtb、相同 desk 卡）：

```text
[exec] /glibc entry=40010609 flags=30
[exec-arm] pc=40010609 flags=30 spsr=30 sp=7FFFEF40
[exec] /desk entry=400155F0 flags=10
[exec-arm] pc=400155F0 flags=10 spsr=10 sp=7FFFEF00
vesper-fbdev: fb 640x480 (frame 640x480), kbd /dev/event0, pointer /dev/event1
vesper-fbdev: running; /init's timeout or Ctrl-C ends the run
[exec] /bin/sh entry=4003D329 flags=30
[exec-arm] pc=4003D329 flags=30 spsr=30 sp=7FFFEF10
trace[43] pdown 1 40 110
trace[48] pdown 1 40 110
trace[96] poll=1 used=0 kbd=0 ptr=0 pty=1
```

日志 `/tmp/desk-fd-serial.log` 无 `exception 1 unhandled`。
额外的 buildroot 卡回归 `/tmp/buildroot-fixed.log` 也进入运行循环：
`/bin/vesper entry=400155F0 flags=10`，实际 SPSR=10。

构建命令 `PATH="/usr/sbin:$PATH" cmake --build build-armv7 --target arm_image -j$(nproc)`
退出码 0。临时 host frame bridge 断言验证了 ARM/Thumb 普通 syscall 状态保留，
以及带旧 T/IT 状态的 trap 被新 exec flags 覆盖。

原始 desk 脚本已实际运行，但当前沙箱拒绝 QMP Unix socket bind：
`Failed to bind socket ... Operation not permitted`，QEMU 启动即退出。
因此原脚本的 MISSING 不能用于判断修复后的 guest 行为。
为得到运行证据，临时脚本 `/tmp/desk-fd-check.py` 用 QMP stdio 和 UART FIFO
启动同一卡及内核，复用仓库 test_armv7_gui_keyboard.py 的点击实现；面板、
循环、点击、pty 四个串口判据均 OK，并输出 `/tmp/desk-fd.ppm`。
这项补测没有替换原脚本结果，也未覆盖原脚本的拖动/多窗口像素断言。

`ctest --test-dir build-armv7 -R "arm_desk|arm_gui_keyboard" --output-on-failure`
退出码 8：0/2 通过，arm_desk 与 arm_gui_keyboard 均失败，总计 39.58 秒。
keyboard 的串口文件明确记录相同的 QMP bind `Operation not permitted`；
desk 原脚本隐藏 QEMU stderr，使用相同 QEMU 参数直接启动已确认相同拒绝。
完整输出保留在 `/tmp/arm-desk-ctest.log`，原 desk 脚本输出在
`/tmp/desk-result.log`（退出码 1，指定的 `/tmp/desk-serial.log` 为空）。
