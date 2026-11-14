# armv7 的 glibc 车站

状态：盘上程序能接力 exec 到静态链接的 glibc 程序，程序用 malloc、printf、
open/read 跑完并退出 0（2026-11-14 夜）。这份记这一站怎么搭起来的、路上修掉
的几个真问题、以及现在还缺什么。

## 现在的样子

开机 → 客人程序走完自检 → exec `/disk/prog`（盘上 freestanding 程序）→ 它
自己再 exec `/disk/glibc`（静态链接的 glibc 程序）→ 该程序打印两行、退出 0。

判定字面量两条：

- `[glibc] hello from ext2, real libc ran`
- `[glibc] malloc, printf, open all ok`

产物是可选件：没有交叉工具链的机器上不铸 `/glibc`，判定字面量也不加，
车道保持绿色。

## 工具链

`scripts/build-armv7-glibc-prog.sh`，按这个顺序找编译器：

1. `CINUX_ARM_CROSS` 指向的 `arm-*-linux-gnueabihf-gcc`
2. PATH 上的 `arm-none-linux-gnueabihf-gcc` 或 `arm-linux-gnueabihf-gcc`
3. `~/.local/opt/arm-cross/usr/bin/arm-linux-gnueabihf-gcc`（Debian/Ubuntu
   的交叉包解包出来的树，binutils 自带的库在 `usr/lib/x86_64-linux-gnu`）

编译参数：`-mcpu=cortex-a7 -marm -mfpu=neon-vfpv4 -mfloat-abi=hard -static
-no-pie -fno-pie -Wl,-Ttext-segment=0x40010000`。最后那个 `-Ttext-segment`
不是随手写的：armv7 的用户窗口从 0x40000000 起，glibc 默认链在 0x10000，
落在内核 identity 映射里，装载器没法在上面建页表。

## 路上修掉的真问题

**取指中止的 lr 少减一个常数。** `_vec3` 存的是硬件给的 lr，那是故障地址
加四。缺页补好之后从 `pc+4` 恢复，函数头被整条跳过，寄存器留着上一个调用
者的值。glibc 静态启动时那次“目的地址是代码页、长度是地址”的 memset 就是
这么来的——反汇编怎么对都对不上号，因为执行的根本不是那里。

**座的缓存世界会过期。** `SeatRepairPage` 原来用座启动时缓存的 space 与
vmas。exec 一换这两个世界，指针就指向旧的，新程序第一条指令的缺页找不到
自己的段。改成每次故障从当前任务现取。

**IFSR 没有写标记。** 取指故障的状态字第 11 位不是 DFSR 的那个写位，照
DSFR 解码会把每次取指都读成写故障，于是只读代码页拒绝补页。

**用户窗口先是 1MiB，后来越界。** 起初 `IsUserSlot` 只认
`0x40000000>>20` 这一个槽，brk 一过 0x40080000 就撞墙。现在窗口是
`[0x40000000, 0x80000000)`：程序与 brk 在低处，mmap 在中间，入口栈在窗口
顶，`kUserTop` 正好落在内核 identity 映射脚下。中途划到 0x90000000 试过，
那条线把内核自己的 0x80000000 代码也划进用户槽，切根就把内核清了。

**libc 起步缺的三道门。** `set_tls`（ARM 私有号 0x0F0005，替 EL0 写
TPIDRURO，EL0 只能读）、`set_tid_address`、`set_robust_list` 与 `rseq`。
另外 `AT_RANDOM` 不能缺席：硬件取不到时退回种子池，否则 glibc 拿着空的
`_dl_random` 去解引用。

**iovec 的宽度归 ABI。** 中立 `HandleWritev` 原来按 x86 的 16 字节项读，
arm 的项是 8 字节，第一次 writev 吐出来的是二进制里的字符串表。现在两套
`abi.hpp` 各自报出向量字节数与字段位置。

**openat。** 现代 glibc 的 `open` 走 322 号 openat，dirfd 是 AT_FDCWD(-100)。
适配器把它折到中立的 open 面；注意那个哨兵是 32 位字，零扩展成 64 位之后
直接和 -100 比会失败。

**mprotect 的中立化。** 中立处理器原来直接用 x86 的条目位。现在两边各自
提供同一组概念：是否映射、是否大页、叶子指向哪一帧、共享与否。x86 用条目
里的 COW 位说共享，armv7 用帧引用计数说共享。顺带修掉 arm 的只读位：
`UserLeafTerms(writable=false)` 以前不写只读位，叶子照样可写。

**退出路径的根。** 清 `clear_child_tid` 那个字时，退出路径可能已经停在根
上，用户名址在根下没有意义。用户内存门现在在拷贝前借出任务自己的页表，拷
完换回来（记录进来时的根，不要硬编码根）。

## 还缺什么

- `mprotect` 只处理 4KiB 叶子，大页跳过；将来设备映射要点到才需要补
- `clock_gettime` 一类计时号仍未接，glibc 目前容忍失败
- `getrandom`、`rseq` 走不支持探针，够用但不完整
- exec 的 argv[0] 是空串（客人传了空 argv），真实程序会想看到自己的路径
- 真正的用户生态（busybox 级）与 GUI 桌面还没上车
