# armv7 端口骨架

状态：能启动（2026-11-12）。记这一版活着的东西与还没接的。

## 这一版有什么

`kernel/arch/armv7/` 下的自有镜像，走 `cmake/arch/armv7.cmake` 的车道，
一次 configure 带一套交叉工具链，root 在车道之后返回，不碰 x86 的构建。

- `start.S`：关中断、给 IRQ 与 FIQ 各配一个栈、开 VFP、清 bss、跳 kmain。
- `uart.cpp`：i.MX6ULL UART1，115200 8N1。UART1 的根时钟按 PLL3 的 80 MHz
  算，UFCR 二分频到 40 MHz，UBIR=15、UBMR=346 得 115273 baud，偏 0.06%。
- `timer.cpp`：通用定时器，CNTFRQ 读频率、CNTPCT 读计数，CNTP_TVAL 定时。
  CP15 的寄存器选择子是关键：TVAL 是 opc2=0，CTL 是 1，CVAL 是 2。
- `gic.cpp`：GIC-400 的 distributor 与 CPU interface，定时器挂在 PPI 29/30。
- `vectors.S` 与 `irq.cpp`：异常向量表，IRQ 与 FIQ 都进同一个 C 处理函数。
- `mmu.cpp`：短描述符的段映射。低 512 MiB 当设备内存（UART、GIC），
  0x80000000 起的 256 MiB 当普通可执行内存，开 MMU 后串口与中断继续工作。

## 验收

`cmake --build build-armv7 --target run` 起 QEMU 的 mcimx6ul-evk，
`ctest --test-dir build-armv7` 跑 `arm_boot`，检查启动行与第一拍 tick。

## 踩过的三个坑

一是安全态。核起来在 secure 状态时，通用定时器的比较中断落在 PPI 29，
默认在 group 0，走 FIQ；只开 IRQ 是收不到的。这一版把 IRQ 与 FIQ 都接上了。

二是异常模式的栈。IRQ 与 FIQ 各有独立的 sp，不配的话处理函数第一句 push
就写进垃圾地址，表现是"中断进了但什么也不打"。

三是 CP15 的选择子。c14,c2 的 opc2 写错一位，读到的是别的寄存器，看起来
像定时器不工作。

## 还没接的

- 两级页表与用户空间：现在只有 1 MiB 段的恒等映射，没有第二级表，
  也没有 USER 域。
- `platform/clock`、`platform/interrupt` 这些中立接口还没接上这一层，
  现在的实现是就地写的。
- 没有把内核核心链进来。mm、proc、syscall 还等着自己的 armv7 实现。
- 没上真板，QEMU 的串口模型比芯片宽松。
