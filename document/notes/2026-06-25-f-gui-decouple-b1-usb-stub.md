# F-GUI-DECOUPLE 批1 — USB 开关归 CMake(usb_stub 空壳)+ init.cpp 消 #ifdef CINUX_USB

> 里程碑:F-GUI-DECOUPLE(GUI 模块独立化,消 main/init/irq 的源码 `#ifdef`,§14)。
> 分支 `feat/gui-decouple`(从干净 main `1e225ff` 拉)。commit `7b2cdcc`。
> 范围决策(用户 2026-06-25):核心 4 批(main+init+irq),test 文件 `#ifdef` 整体守卫留 follow-up。

## 背景

CODING-TASTE §14 铁律:开关归 CMake,源码读起来该是「全编进去也读得通」。
F-CLN 已清 keyboard/pit 的 `#ifdef CINUX_GUI`;CMake 文件级 gate 框架已就位
(drivers/gui 按 `if(CINUX_GUI/CINUX_USB)` 选编)。剩 main/init/irq 三处源码
`#ifdef` 读半截路 = §14 真违规,本里程碑收敛。本批先做 USB(最独立、风险最低)。

## 改动

| 文件 | 改动 |
|------|------|
| `usb_init.hpp` | 加 `usb::poll_input()` 声明(每帧轮询事件环,空控制器/USB 编译关闭时 no-op) |
| `usb_init.cpp` | 加 `poll_input()` 实现 = 封装原 `XHCIController::has_controller() + instance().poll_events()` |
| `usb_stub.cpp`(新) | 非 USB 编译时的空壳:`usb::init(){}` + `usb::poll_input(){}`(§14 规矩1) |
| `drivers/CMakeLists.txt` | USB gate 加 `else() → usb_stub.cpp`(原来无 else,非 USB 时 init.cpp 调用无定义) |
| `proc/init.cpp` | 消 3 处 `#ifdef CINUX_USB`:include 块→无条件 `usb_init.hpp`(删 xhci_controller.hpp);gui_worker 内 `#if defined(CINUX_USB)` poll→无条件 `usb::poll_input()`;USB init 块→无条件 `usb::init()` |

## 设计点

- **`usb::poll_input()` 封装**:gui_worker 原直调 `XHCIController` 静态方法 + 带 `#if defined(CINUX_USB)`
  守卫。收进 usb 模块的自由函数后,(a) init.cpp 不再依赖 `xhci_controller.hpp`;(b) 调用处变一行普通
  调用,零 `#ifdef`;(c) 非 USB 时空壳顶上,链接器选一份。
- **CMake file gate(§14 规矩1 正解)**:`if(CINUX_USB) usb_init.cpp... else() usb_stub.cpp`。
  调用方(init.cpp)零 `#ifdef`,读起来一条直线。对齐 F5-M5 既有的 USB 正例风格。

## 验证矩阵

- 默认(GUI+USB on)`run-kernel-test`:**931/0** ALL PASSED
- 默认 `make run` GUI 冒烟:零 panic(xHCI keyboard armed + USB input primary + cgui adapter init + desktop composite)
- **非 USB 生产 `big_kernel` 构建**:绿(usb_stub.cpp 链接 OK,init.cpp 调用解析)

## 边界 / 预存问题(follow-up)

- **非 USB `big_kernel_test` 链接失败**:`test_xhci.cpp` 无条件进 big_kernel_test 源列表且
  **无 `#ifdef CINUX_USB` 守卫**,非 USB 时引用 `XHCIController` 等未定义符号。这是**预存问题**
  (批1 前就破),属 test gate(本里程碑批5 follow-up,用户已拍板 test 留 follow-up)。
  本批只负责**生产代码**非 USB 兼容(big_kernel 已验)。

## 下批

批2:`launch_userspace()` 抽象 —— 把 init.cpp 的「GUI 启桌面 / 非 GUI fork shell」二选一
用户态启动收成单一接口 + 两实现文件(desktop_launch.cpp / shell_launch.cpp)+ CMake 选编,
消 §14 头号反例(kernel_init_thread 读到一半分叉两条路)。
