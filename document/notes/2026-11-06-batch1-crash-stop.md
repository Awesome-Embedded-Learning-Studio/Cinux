# 2026-11-06 批一收工:崩溃止血四项

## 交付

- `2e40e48` getdents 对非目录描述符返回 ENOTDIR。sys_getdents.cpp 在 `ops->read_dir` 为空时拒绝,test_syscall_face 补回归(管道两端、eventfd、epoll 各返回 -ENOTDIR)。
- `c166782` loopback 队列满改为丢帧计数。kLoopbackQueueFrames 4→32,net_world.cpp 两处致命 Check 改为丢弃并计 `dropped()`,test_net_loopback 与 test_net_socket 各补一条满队列测试。
- `dfae286` 就绪队列满改为拒绝调度并计数。kReadyQueueCapacity 16→64,scheduler 五个入队点(seat/yield/maybe_preempt/unblock/resume)失败时任务保持原状态并计 `ready_refusals()`,新建 test_sched_row 覆盖满队拒绝与恢复。
- 本次:驱动取页整组判空。xhci `arm_rings` 三页、usb_device `address` 四页、ahci 抽出 `take_port_pages()`、e1000 抽出 `take_ring_pages()`,任一页为零地址即退回已拿的页并返回 false,设备缺席,内核照常跑。usb_device 的取页挪到 `seat_device` 之前,失败路径不再留下已入座的设备;槽位禁用交给调用方已有的 detach。

## 验证

- 全量编译通过;boot_ktest_block / block_error / net / keyboard 双腿(cpu1/cpu2)全绿,这四个测试走的正是被改四个驱动的 init 路径。
- 前三项每项做了旧代码复现:stash 内核改动跑回归,分别拿到 139、134(带 assert 行号)、134,再恢复改动转绿。
- 驱动判空一项没有运行时复现:失败路径要物理内存耗尽才触发,验证止于代码层加启动路径。
- ASAN 全部 host 单测通过,以 `ASAN_OPTIONS=detect_leaks=0` 绕开一个旧测试自带的泄漏(test_net_socket.cpp:536,15104 字节,遗留另议)。

## 对齐说明

- socket.cpp:602 的 `new` 由 operator new 的全局 Check 兜底(堆耗尽即停机,全局策略),无需改动。
- 方案讨论过"整组退回助手"与"页回收/OOM killer"。前者落地为四处就地检查退回;后者定为下一代机制,本批不做。对齐的 Linux 语义:驱动 probe 取页失败返回 -ENOMEM 设备缺席,OOM killer 属于可睡上下文的慢路径,启动期没有可杀对象。
