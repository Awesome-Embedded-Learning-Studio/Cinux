# 内核测试基建

## 功能概述

这一页咱们把 Cinux 的测试基建完整地走一遍。今天的测试分成两个世界，host 测试的家在 `test/unit`，编出来的是普通的可执行文件，在咱们开发用的电脑上直接跑。ktest 是 kernel test 的缩写，它编进的是内核镜像，开机的地方是 QEMU，用例安的家是 `test/kernel`，每场用例文件都配一份自己的内核。两个世界共用的是同一份框架的一对孪生，用例记录、断言宏与失败报告的分工出自同一批声明，差的只是注册与退场的方式。`test/framework` 是这对孪生的家，`test/mock` 摆的是内存盘与引用计数这一类测试玩具，`test/tools` 挂的是 CI 闸门自己的自检脚本。host 侧的链接骨架与断言钩子的 host 定义，[内核库](lib.md) 已经摆过一遍了，这一页接的是它留的口子，把三类链接怎么分、怎么加一个测试、ktest 怎么装配怎么跑讲完。整套本地检查与基线的跑法收在 [测试与验收](../testing.md)。

规模咱们报个实数。`test/CMakeLists.txt` 的名单今天挂了八十四场 host 测试，ktest 的名单在 `cmake/test/kernel_catalog.cmake` 里，今天的场次是二十二场，每场在 CTest（CMake 的测试驱动）里拆成一到两条腿，按的是 CPU 数。数目是会跟着开发走的，咱们引用数目的时候，都以仓库里的名单为准。

## 一份用例记录，两个 runner

记录本身小得让咱们意外。`test/framework/test_case.hpp` 里的 `TestCase` 只有两个指针，装的是名字与函数体，注释说它是刻意的纯数据：内核世界把记录摆在一个静态构造函数轮不到的地方，它身上什么都带不了。两个世界的 runner 走的是同一种记录，差别只在注册的方式，头文件单独放一份的原因注释也讲了，排布要是两边漂移了，坏是悄无声息地坏，单头文件就是让孪生成其为孪生的保险。

host 世界的注册骑在静态构造期上。TEST() 宏展开出来的，是一个函数加一个 `Registrar` 对象的组合，对象在构造的时候把用例写进 256 槽的注册表（`kMaxCases`，`framework.cpp` 里的常量）。`RunAll()` 把用例逐场地跑，名字后面跟着的是 `[PASS]` 或 `[FAIL]`，返回的失败数就是进程的退出码。框架不发的是 `main()`，每场测试自己带自己的入口，多数入口只有这一句现成的 `return cinux::test::RunAll();`。不过例外是有的，`test/unit/test_uaccess.cpp` 的 main 得把测试页 mmap 出来，等全部用例跑完了再用 munmap 把页还回去，交回来的仍是 `RunAll()` 的结果。一场算不算过的问题，咱们拿快照语义回答：跑这场之前咱们记下失败计数，跑完之后计数没动过的才算过。framework.hpp 的注释把这个来历讲得很坦白，v1 的框架曾经给每场无条件地标 PASS。快照规则就是为这个 bug 立的，`framework` 那场套件守的就是它。

内核世界是零全局构造的世界，静态构造函数在那里是活不成的。TEST() 在这边把记录直接放进 `.test_cases` 输入节（链接器归置目标文件同名列的地方），`kernel/arch/x86_64/kernel.ld` 用 `g_test_start` 与 `g_test_end` 两个符号把这段的范围围了起来。`RunKernelTests()` 在开机之后从表头走到表尾的这段路，靠的全是链接器摆好的纯数据，构造、堆与初始化次序都是它不碰的东西。runner 在每场执行之前打出的是 `[RUN]` 行，它的用意咱们把注释读一遍就明白了，framework_kernel.cpp 讲的是某个被写坏的全局把机器带走的时候，串口上的最后一行就是凶手的名字。总结打完了以后，`exit_qemu` 把失败数加一的值写进 0xF4 口，isa-debug-exit 是 QEMU 的调试设备，写一个字节进去 QEMU 就退出了，退出码按 (值<<1)+1 的公式算，全过的退出码就是 3。空表的镜像是什么都不跑的，函数当场就返回了，机器就停在 `Halt()` 上了，等它的是计时器。

## 断言的孪生

断言这块咱们数出了两个孪生对，一层一层地认。下层的一对在 [内核库](lib.md) 里讲过了，base 的 `Check` 内联在头文件里，`AssertionFailed` 是各世界交各世界的定义：host 的一份在 `test/framework/assert_host.cpp`，打印了位置之后调 `abort()`。内核的一份在 `kernel/lib/assert.cpp`，panic 之后机器就停了。上层是测试框架自己的 ASSERT_* 族，声明的家在 `test/framework/test_assert.hpp`，这一层咱们在这里展开。

共享的宏有十一张面孔，从 `ASSERT_TRUE`、`ASSERT_FALSE`、`ASSERT_EQ`、`ASSERT_NE`、`ASSERT_NULL`、`ASSERT_NOT_NULL`、`ASSERT_GE`、`ASSERT_LE`、`ASSERT_GT`、`ASSERT_LT` 排到 host 独有的 `ASSERT_STREQ`，字符串比较没有内核孪生的说法，它就留在 framework.hpp 里不动了。它们必须是宏的原因，咱们把注释里的理由念一遍，失败后那记提前的 `return` 要离开用例体，能做到这一点的只有文本展开。宏做的只有算一遍表达式、报一次失败这两件事，报的活交给 `ReportCheckFailure` 与 `ReportCompareFailure` 两个钩子：声明是一份的，定义是两个世界各拿一份的，链接的时候挑。host 的定义在 `framework.cpp`，输出走的是 stderr。内核的定义在 `framework_kernel.cpp`，走 `Println` 上了 COM1。报告只带表达式的原文与文件行号，值是不带的，注释给的缘由是两个世界说不到一起去，host 拿来比较的类型，内核里连它的名字都可能叫不出来。

这层孪生与下层 Check 的分工咱们分清。ASSERT_* 记的是一次失败、当场从用例体返回，runner 接着跑它的下一场，出事的那一场到不了后面的断言，别的场是照常走的。被测代码里的 `Check` 命中是停机级的事件，host 上进程 abort 了，内核上机器 panic 了，出事位置之后的一切都没有了。写用例的时候咱们心里要有界线，想让失败被逐场报告的，用的就是 ASSERT_*，命中的瞬间整个世界都该停下来的那类检查，交给的才是 `Check`。

## host 测试的三类链接

`add_cinux_test` 的完整骨架 [内核库](lib.md) 引过了，这里讲的是它的用法。用例文件的名字是函数定死的，`unit/test_${name}.cpp`。框架的四个文件每场必链，链的是 `framework.cpp`、`assert_host.cpp`、`per_cpu_host.cpp` 与 `irq_guard_host.cpp`。内核源文件从 `${ARGN}`（CMake 函数的多余参数）的位置往后挂，链接的是 `cinux_base` 与 `cinux_warnings` 两个库，末尾的 `add_test` 把它注册进 CTest，注册用的名字就是传进函数的头一个参数。咱们往名单里挂一行，configure 配了一遍之后，ctest 就认得它了。

第一类是纯 base 的直链，八十四场里有三十五场走的都是同一条路。咱们写 base 自己的语义验证，落的都是这类。`format`、`result`、`bits`、`bitmap`、`checksum`、`path`、`self_list`、`slot_table`、`framework` 这些套件的名单里连一个内核源文件都没有。头文件里的内联件由编译器实例化出来，跑的就是普通进程。它们验证的是 base 自己的语义，与操作系统无关的那部分，断言钩子换成 host 的定义，同一套用例在哪个世界都是说得通的。

第二类直链的是内核源文件，咱们数得出的有 `pmm`、`heap`、`vfs`、`lockdep`、`tty`、`ext2`、`scheduler` 这些，挂的都是 `${CMAKE_SOURCE_DIR}/kernel/...` 的路径，能这么挂的前提 [内核库](lib.md) 讲过了，那些 .cpp 碰的是 base 与测试框架，寄存器是不碰的。[堆管理](mm/heap.md) 那一页提过这层设计的来意，`Heap` 的算术不碰页表，host 测试拿 64 KiB 的静态缓冲当池子，分配的语义就能整个过一遍。unit 的 heap 套件今天有六场用例，init 把整段认成一个空闲块、对齐的载荷、归还时的合并、首次适配的复用、拒收野指针、grow 追加空间，[物理内存管理（PMM）](mm/pmm.md) 的 `pmm` 套件与它的形状是类似的。调度器那场直链了十个内核源文件，用的是 RecordingSink 把决策记成流、逐条断言的做法，[进程与调度器](proc/process_and_scheduler.md) 讲了它为什么成立。

第三类走的是真线程，咱们点到名的是 `input`、`tick`、`ref_count`、`scheduler`、`file_table` 五场，链的都是 `Threads::Threads`，争用就发生在线程真的同时跑的条件下。撑着它们的是 `per_cpu_host.cpp`，`PerCpu::self()` 在 host 上实现成 thread_local（每个线程一份的存储），一个进程里的每个线程各有一块自己的 per-CPU，`smp.hpp` 的 `NotifyRunnable` 在 host 上是空函数。`irq_guard_host.cpp` 给的是中断开关的替身，`SaveAndDisableIrq` 返回的值带着 IF 位的样子，轮到 `RestoreIrq` 的时候就是空转，守卫代码照常地走，只是没有真的中断可关。`syscall_host.cpp` 与 `clock_host.cpp` 两件只出现在 `syscall_face`、`signal` 与 `ps2_controller` 三场的名单里，前者把 `CurrentTask`、`LiveSlot` 这些接缝换成 host 的回答，后者拿的钟是 `std::chrono`。uaccess 的使能位在 host 上不开，窗口就永远是关着的，它是不需要替身的，syscall_host.cpp 的注释把这层写明了。

## 内核态测试：ktest

ktest 的名单咱们到 `cmake/test/kernel_catalog.cmake` 里看。一条登记写的是 `名字:梯级` 的格式，注释把它的形状讲完了，一个用例文件配的是一份内核与一份镜像，一次 QEMU 引导对应的就是一场 CTest。今天的名单咱们数出了二十二场，按梯级归拢的是下面的样子：

- selfcheck、console 走最素的 exceptions
- tick 在 irq，keyboard、tty 在 keyboard
- vmm、heap、aspace、cow、vma、demand、loader 七场在 mm
- uaccess、sched、pipe、smp、smp_pipe 五场在 proc
- block、block_error、ext2、xhci、net 五场在 block

梯级的名字一共六个，咱们把它们分两半记，是 exceptions、irq、keyboard 的三档，加上 mm、proc、block 的三档。梯级（源码注释里的 stage），说的是 test main 在跑用例之前内核初始化装到的最高一层，登记里写了名单外的名字，configure 当场报的错就把构建拦下来了。

梯级管的是用例开跑之前内核装到哪一层这件事，说了算的是 `test/kernel/test_main.cpp` 里编译条件的分组，咱们把它当地图用。MM 这一组装的是串口、PMM、地址空间与异常表，NX 位、uaccess、console 与堆也是这一组的。串口排的是整条链的头一个位子，PMM 起不来的时候，那句 `[ktest] pmm init failed` 报的就是它，[串口驱动](drivers/serial.md) 是整条链的头一环。IRQ 组在这之上装的是 PIC 的重映射与中断桩，PIT、Tick、APIC 后端与 IRQ 线 0 也在它的名单里，[PIT 定时器](drivers/pit.md) 有它的专页。PROC 组再装两个调度类与内核的切换 sink，sink 是调度器把每次切换报出去的那一头，[进程与调度器](proc/process_and_scheduler.md) 讲过这套记录的做法。keyboard 梯级挂的是键盘的初始化与 attach，[键盘驱动](drivers/keyboard.md) 是它的正页。block 梯级把中断打开之后走的是 PCI 与 AHCI，它要的 ext2 测试盘不在的话，打印完提示就停机了。exceptions 是最素的一档，装的是帧缓冲的接线与 console 的初始化，selfcheck 那场住的就是这一档，查的是 GDT、IDT、可恢复的 ud2 与 kallsyms 四件事。可恢复的 ud2，说的是触发 ud2 之后从处理口恢复执行的自检。kallsyms 是 panic 回溯用的内核符号表。MM 这一组装完的时候，`Main` 打出的下一行就是 `[ktest] kernel test world alive`，proc、keyboard、block 这三档在它之后还会各打一行梯级的报行，报行都打完了，然后 `RunKernelTests()` 就上场了。

构建的管线咱们到 `kernel/CMakeLists.txt` 的 `add_cinux_ktest` 里看。整份内核核心源码打成的是 OBJECT 库，它出的只是目标文件，库文件是它不单独出的。接上的是 `test_main.cpp`、`framework_kernel.cpp`、字体 blob（内核显示字库的二进制）与这一场的用例文件，还有一个自动生成的 kallsyms 桥，panic 回溯要用的符号表就从它编进内核。编译定义给的是 `KTEST_STAGE_<大写梯级>` 与 `ENTRY_SYMBOL=KernelEntry` 这一对。ELF 削成 .bin 的活归 `objcopy`，boot 侧的 `add_cinux_image` 把 .bin 包成 `boot_ktest_<名字>` 的磁盘镜像，每张镜像依赖的都是 `ext2_ktest_img`，QEMU 挂盘参数里的 `boot/ext2.img` 也被换成了 `boot/ext2_ktest.img`，块梯级要的盘就从它来。镜像与正式内核走的是同一条引导链，差别出在入口之后装多少层的取舍里，[引导链](../boot.md) 是那半边的故事。

heap 那场 ktest 的用例咱们数出三场。头一场验的是 `new` 与 `delete` 的往返，第二场拿 100 KiB 的大块把 span（堆占着的那一整段虚拟内存）顶高一截，第三场看的是释放后的槽位被下一次分配复用，三场的判定都靠断言收尾。

## 怎么加一个测试

host 要加一场的话，写的是 `test/unit/test_<名字>.cpp`，用例的起手是 TEST()，文件末尾要自带的是 `int main()`，返回的是 `RunAll()` 的结果。然后咱们到 `test/CMakeLists.txt` 挂一行 `add_cinux_test(名字)`，要测的代码是内核源文件的话，挂的位置是参数里，要压并发的话，链上的就是 `Threads::Threads`。configure 配完了，ctest 的名单里就有它了，新增的名字 `test-inventory.py` 是认的，对上了才放行。

ktest 加的一场多的是登记这一步。咱们写 `test/kernel/test_<名字>.cpp`，include 的是 `framework_kernel.hpp`，TEST() 的形状与 host 的一模一样，记录进的是链接节。然后咱们到 `cmake/test/kernel_catalog.cmake` 的 `CINUX_KTESTS` 挂一行 `名字:梯级`，梯级挑的是六个名字里的一个，挑的标准是把这一场要摸的硬件装齐的最低一档，要限单腿的话，写进 `CINUX_KTESTS_DUAL_CPU` 的是名字。删除与改名要的就是显式更新 `test/ctest-baseline.json`，这层核对规则的出处是 `scripts/checks/README.md`，基线锁的是注册名单与 CPU 腿，用例内部的断言数目它是不数的。

一场用例讲一件事就够了，ASSERT_* 挂了之后当场返回，写在后面的断言是不执行的，所以有前后依赖的检查，最要紧的摆在前头。这个次序是框架不强制的，它是快照语义下的自然结果，咱们把习惯写在这里，为的是给后来的人省一次试错。

## 跑起来

build 目录配好了之后，咱们拿 `cmake --build <build> --target test_host` 把 host 的全部测试跑完，ctest 的并发数从环境变量 `CINUX_CHECK_JOBS` 来，默认的是 2。ktest 的总闸是 `--target test_kernel`，它按 `-j 1` 串行地跑，因为每场要的都是独占的 QEMU。单独看一场的话，host 用的命令是 `ctest --test-dir <build>/test -R 名字`，ktest 这边的命令是 `-R '^kernel_test_名字_'`。仓库自己的完整一套在 `scripts/checks/run-cell.sh` 里，它把配置、基线核对、构建与 ctest 串成了一趟，host 与 kernel 是分开的两个 cell，cell 就是 run-cell.sh 跑的一整套检查的叫法。

腿与硬前提咱们讲清。ktest 每场默认跑的是 cpu1 与 cpu2 两条腿，block、smp、smp_pipe 三个只跑 cpu2 的腿，这是 `CINUX_KTESTS_DUAL_CPU` 定的。每条腿的超时是 120 秒，`RESOURCE_LOCK cinux_qemu` 保证并行调用 ctest 的时候 QEMU 仍然串行。ktest 对 KVM 的态度分两层，咱们分开认。判卷脚本 `run_kernel_tests.cmake` 这层给咱们留了后手：咱们在容器或者没开嵌套虚拟化的 WSL2 里摸不到 `/dev/kvm` 的时候照样能把 ktest 跑起来，因为脚本给咱们只发一条 WARNING，就把 `-accel kvm` 改写成 tcg（QEMU 的纯软件模拟后端）、`-cpu` 改写成 max，注释的原话咱们照录：`rewrite the pair to TCG instead of refusing to boot`。TCG 逐条模拟指令所以跑得慢，咱们看超时也就从 60 秒放宽到了 100 秒。验收那层对咱们才是硬要求，scripts/checks/README.md 写的是 QEMU 必须有可访问的 `/dev/kvm`、不回退 TCG，沙箱或者容器拦 KVM 的话，咱们就换允许的环境重跑。退出码 3 只是及格线的一半，脚本还要在串口日志里找到至少一场通过、零失败的总结行，空跑与失败数被截断的假绿都过不去，假绿是 scripts/checks/README.md 里的原话。退出码比 3 大的时候，脚本按 (码-3)/2 的算式算出挂了几场，退出码 1 是没走到退出口的早夭。串口日志留在每条腿自己的目录里，出了事，要看的验尸报告就是它，这句是脚本注释的原话。

保险的另一层在 `test/tools/test_ci_checks.py` 里，它拿合成出来的进程演练 CI 的失败闸门，文件头的原话是不冒领 KVM 的覆盖。咱们改 `run_kernel_tests.cmake` 或者 catalog 的时候，改坏了的话，接住咱们的就是这场自检。

## 源码位置

- `test/framework/test_case.hpp` — 两个世界共用的一份用例记录
- `test/framework/framework.hpp`、`framework.cpp` — host 的注册表、runner 与 stderr 失败报告
- `test/framework/framework_kernel.hpp`、`framework_kernel.cpp` — 内核孪生：.test_cases 节、COM1 报告、isa-debug-exit 退场
- `test/framework/test_assert.hpp` — 共享的断言声明与 ASSERT_* 宏
- `test/framework/assert_host.cpp`、`per_cpu_host.cpp`、`irq_guard_host.cpp`、`syscall_host.cpp`、`clock_host.cpp` — host 世界的替身定义
- `test/unit/`、`test/kernel/` — host 用例与 ktest 用例，`test/kernel/test_main.cpp` 是 ktest 的开机装配
- `test/mock/`、`test/tools/` — 内存盘与引用计数的玩具、CI 闸门自检
- `test/CMakeLists.txt` — `add_cinux_test`、Threads 名单、`test_host` 与 `test_kernel` 目标、ktest 的 CTest 腿
- `cmake/test/kernel_catalog.cmake` — ktest 名单与梯级
- `kernel/CMakeLists.txt` 的 `add_cinux_ktest`、`boot/CMakeLists.txt`、`kernel/arch/x86_64/kernel.ld` — ktest 内核的装配、镜像与 `.test_cases` 围栏
- `cmake/test/run_kernel_tests.cmake` — QEMU 的运行与退出码核对
- `test/ctest-baseline.json`、`scripts/checks/test-inventory.py`、`scripts/checks/run-cell.sh` — 注册基线、核对与整套 cell
