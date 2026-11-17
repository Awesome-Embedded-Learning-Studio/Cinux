# 用户态 C 库

## 功能概述

这一页咱们把 ring 3 手里的 C 库家当盘点一遍。Cinux 的用户态今天住着两个世界。一界是仓库里自己写的 freestanding（不带操作系统与完整标准库的编译环境）程序，它们不链任何现成的 libc，家当是一份入口汇编、一层系统调用桩和几枚与内核共享的头文件。另一界是 buildroot（把完整的 Linux 用户态根文件系统自动装配起来的构建框架）装进开发盘的真实 glibc 世界，BusyBox、机内的编译工具链、桌面程序都在那边过日子。两界踩的是同一套系统调用约定，这一页的主线就是把约定本身认清楚，再把两界各自的家当点一遍，材料全部对着当前的源码来。

兄弟页的分工咱们在开头摆明白。系统调用进了内核之后的查表与指针搬运归[系统调用](../kernel/syscall.md)，fork、execve 与 wait 的装载细节归[进程的复制与替换](../kernel/proc/fork_exec.md)。base 库与内核自己的内存函数归[内核库](../kernel/lib.md)，描述符的号码与打开描述的分工归[文件与路径](../kernel/fs/file_and_path.md)。write 与 read 背后的设备面孔归[Terminal 终端](../kernel/gui/terminal.md)，shell 程序自己的命令面归[Shell](shell.md)。这一页只认用户态随身带的这部分家当，越界的地方咱们都回指。

## 入口与链接

在树的这些用户程序全走一个模子，每个程序的编译名单里都塞着一份 start.S。入口 _start 的座位在 .text.start 段，干的活咱们直接对源码念：

```asm
# user/start.S（节选，段指令略）
_start:
    xorq %rbp, %rbp
    call UserMain
    movq $60, %rax
    xorq %rdi, %rdi
    syscall
    ud2
```

rbp 清了零，控制权交给程序自己的 UserMain，等它交还的时候，入口就地把 exit 系统调用发了出去（60 号，跟 Linux 的 exit 同一个号码），末尾的 ud2 是兜底。所以每个用户程序都得交出一个 `extern "C"` 的 UserMain，hello.cpp 的全文就是一份问候加一次退出，咱们把它当最小的样例：

```cpp
// user/programs/hello.cpp
#include <api/syscall.hpp>

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" void UserMain() {
    const char* greeting = "hello from the volume\n";
    user::Write(1, greeting, 21);
    user::Exit(42);
}
```

hello 的问候走的是 1 号描述符，shell 的键盘输入从 0 号进来，号码背后的设备面孔归[Terminal 终端](../kernel/gui/terminal.md)，号码与打开描述的分工归[文件与路径](../kernel/fs/file_and_path.md)，各自的页里咱们都能读到细节。

编译与链接的名单住在 user/CMakeLists.txt，咱们把它分成编译与链接的两半看。编译的旗标摆着 -ffreestanding、-fno-exceptions、-fno-rtti、-fno-pie、-fno-stack-protector、-mcmodel=small 与 -mno-red-zone，标准选的是 C++23，优化开的是 O2。链接的选项更说明问题，-nostdlib 交代给链接器的话是不找任何标准的启动文件与库，这正是 freestanding 的另一面，-static 加 -no-pie 出的是静态的定基镜像，入口的位置交给 `-Wl,-e,_start`，补上的 -lgcc 是编译器的算术辅助库，除法一类的帮手从这里拿。头文件的搜索路径一共三条，user/ 让 api/syscall.hpp 这样的相对路径找得到落点，仓库根的路径让内核头能直接进用户程序，base/include 供的是 base 的工具头。

start.S 由构建里的 foreach 一轮塞给每个程序，所以每个 ELF 的入口是一副模子，咱们换程序的时候，换的只有 UserMain 里的内容。

仓库里还留着一份 user/linker.ld 的链接脚本，它给用户镜像定的座位是 USER_VMA = 0x400000。咱们对着构建名单核对过，当下的用户链接没有把它递给链接器，走的是工具链的默认脚本，而 -no-pie 的静态镜像，在默认脚本里的基址恰好就是这个 0x400000。定基的形状偏移取零、位置无关的形状拿基址加随机滑移，这套装载的走法在[进程的复制与替换](../kernel/proc/fork_exec.md)的装载段里讲过了。

## 系统调用桩

真正把用户程序与内核缝在一起的是 user/api/syscall.hpp。文件头的自我定位写得直白，libc 的名分它不要，打算做的也不是 libc 的活，每个 wrapper 就是一条 syscall 指令加上 Linux x86_64 的寄存器约定，号码从内核自己的头里来，两个世界读的是同一张表。底座咱们认的是 Syscall3，全身摆出来给大伙看：

```cpp
// user/api/syscall.hpp（节选）
inline long long Syscall3(unsigned long long number, unsigned long long arg1,
                          unsigned long long arg2, unsigned long long arg3) {
    // NOLINTNEXTLINE(misc-const-correctness)
    long long ret = 0;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
                     : "rcx", "r11", "memory");
    return ret;
}
```

号码放进的是 RAX，参数按 rdi、rsi 与 rdx 的次序排开，返回的值由 RAX 带回来，咱们对着内联汇编一眼就能认出来。syscall 指令的动作是把返回地址与 rflags 分别接收到 RCX 与 R11 里，所以 rcx、r11 与 memory 在内联汇编里一起报成了 clobbered（告诉编译器这些值不作数了），编译器也就不会把活值横跨 syscall 指令存放了。六个参数的变体是 Syscall6，四个参数的变体是 Syscall4，第四个参数从 RCX 挪到 R10 的缘故写在注释里，syscall 指令把真 RCX 接管了过去，ABI 就只好让 R10 顶了上去。

桩的名单咱们按题目分组看。文件与路径的起居由 open、close、mkdir、unlink、getdents、stat、chdir 与 getcwd 伺候，进程的生死与交接归 fork、clone、execve、wait4 与 exit 操心，sigaction、sigprocmask、kill 与 setpgid 应对的是信号与进程组，地址空间的承诺与收回托付给了 mmap、munmap、mprotect 与 brk，身份与同步的小件落在 getuid、gettid 与 arch_prctl 手里，set_tid_address 与 futex 搭的是另一条交接的路，等的一面是 FutexWait，叫醒的一面是 FutexWake。线程死的时候内核清掉 tid 字、唤醒等着的人，往下到讲 ABI 头的段落，咱们从 child-clear-tid 的那一侧再接一次。每个 wrapper 都是一层薄薄的壳，做的是号码换算加指针搬运的转手活，错误的答案以负的 errno（错误号，libc 按它设置自己的 errno 变量）回来。rt_sigaction 的 wrapper 还按 Linux 的约定，在第四个参数里捎着信号集的字节数。

名单里最费笔墨的是 Clone。fork 给的是整份复制的子进程，Clone 给的可以是线程，也可以是按需定制的进程，flags 的共享位决定了什么共享什么复制，kCloneVm 共的是地址空间，kCloneFiles 共的是描述符表，kCloneThread 并的是线程组，kCloneSetTls 立的是线程指针。它的内联汇编同时照顾父与子两侧，咱们把文件里的注释转述一遍，子进程落在新给的栈顶、回到同一条指令上，落点若是一句普通的 C 收尾就会把垃圾弹出来。所以子进程的一侧清掉了 rbp，把 r15 里的参数放进第一参数寄存器 rdi。栈按十六字节对齐了，call 进 r9 带来的入口，入口函数的尽头安排的是 Exit。父进程的一侧顺着标签往下走，把返回值带了回去。rdx 与 r10 按 Linux 的座位捎着两个 tid 字，r9 与 r15 捎的则是入口与参数，内核保管的是除 rax 以外的全部寄存器。

exit、reboot 与 futex 这三件咱们也认一下。exit 的 wrapper 标着 [[noreturn]]，体内还放着 __builtin_unreachable 的保底，start.S 走的是自己的直线，它把 60 号直接装进了 rax。reboot 的 wrapper 把内核头里的两个口令字（kRebootMagic1 与 kRebootMagic2）加断电命令值按位置装好，机内 poweroff 收尾时走的就是它。futex 的两个面也备齐了，FutexWait 拿四字节的用户字停车，字要是已经变了，回来的是 -11（EAGAIN），FutexWake 负责叫醒停着的队列，注释把这些行为都写在函数头上了。

## 与 Linux 共用的 ABI 头

用户程序直接 include 内核的头，这件事是整套设计的枢纽。smpstress 与 shell 的名单里都带着 `kernel/syscall/syscall.hpp`，sigdemo 与 sigcatch 带的是 `kernel/syscall/signal.hpp`，mapper 还带了内核架构目录的 context.hpp，因为 include 的搜索路径里仓库根本来就在座。syscall.hpp 的文件头把约定写了出来，号码照的是 Linux x86_64 的编号，寄存器的用法照的也是它，错误以负的 errno 回去，这套约定就是平常说的 ABI（应用二进制接口，约定的是二进制程序与内核之间的呼叫方式）。注释往下还多说了一句，未来的 musl 与它背后的整个 rootfs 对这套约定是不问自取的，咱们写用户程序时占的是现成的便宜。

号码的实况咱们数几个认识的。read、write 与 exit 的位置是 0、1 与 60，fork 与 execve 占的是 57 与 59，getdents（读目录项的系统调用）的位置是 217，openat 的号码是 257。errno 的词汇表住在同一个头文件里，kEnosys、kEbadf 与 kEagain 的值是 38、9 与 11，跟 Linux 的原表一字不差。完整的号码表加内核侧的分发走读归[系统调用](../kernel/syscall.md)，咱们不重念。

字节布局的承诺比号码还要硬。SyscallStat 按的是 Linux x86_64 的 144 字节布局，static_assert（编译期断言）的断言行把偏移逐个看着，从 inode 的 +8 一路看到 size 的 +48，链接数与 mode 的座位也在名单里，咱们数过的这几处都有断言守着。getdents 的目录记录按偏移手摆成 dirent64 的形状，inode 的位置在 0，续读 cookie 的位置在 8，记录自己的长度在 16，类型字的位置在 18，名字的字节序列从 19 开始。注释把原因也讲了，十九字节的头按结构体的自然对齐会垫到二十四字节，所以字段的位置由常量逐个定好，记录的排法是彼此贴着，整段的长度都是八的倍数。SigAction 是四个字的记录，handler、flags、restorer 与 mask 的次序照 Linux 排，守着它的也是同样的断言。

偏移对得这么死的缘由，咱们放到消费的那一头去找。glibc 的二进制拿固定偏移读这些记录，错了一个字节，读出来的就是乱码。内核头的注释也是这么定位的，SyscallStat 的说明写着，libc 消费的就是全量的记录，字段的偏移是它的读法。

旗位与口令字咱们核下来的，同样是 Linux 的原值。open 的 O_CREAT 是 0x40，O_CLOEXEC 的值是 0x80000，clone 的 kCloneVm 与 kCloneFiles 是 0x100 与 0x400，mmap 的 MAP_ANONYMOUS 是 0x20，reboot 的两个口令字取的是 0xFEE1DEAD 与 672274793，AT_FDCWD（相对路径的基准哨兵，-100 表示按调用者的工作目录解析）给的是 -100。glibc 发的是原值，内核应的也是原值，中间不需要翻译的一层。

信号递送的两个接缝咱们停一步看。SA_RESTORER（0x04000000）在内核这里是被认真应答的，signal.hpp 的注释交代了用意，glibc 装的每个 handler 都带上 SA_RESTORER，restorer 指向它自己那段以 rt_sigreturn 收尾的代码，内核照它的指示走。freestanding 的程序不设 restorer，内核在 exec 的时候用 MapSignalTrampoline 把蹦床页映进新的地址空间，注释给它的身份是兜底。set_tid_address 与 clone 的 child-clear-tid 位搭起来的是另一条交接，线程把自己的 tid 字报到内核，死的时候内核清字并唤醒等着的人，clone 的旗位注释把这层写作 join 的握手，死去的那一侧由 futex 应答。

## 最小的 C 家当

boot/early/libc.cpp 是引导、内核与用户态三个世界共用的小家当，文件里的两个函数咱们挨个认。strlen 数的是 NUL 结尾的字符串，__udivmoddi4 算的是 64 位除法带余数。文件头的注释把身份交代了，这些 C ABI 名字是有意压住库里同名的，它们是咱们自己的。__udivmoddi4 这个名字来自编译器的除法辅助词汇，libgcc 里本来就有一份同名的，smpstress 的构建把它列为直接的成员，编译器要发 64 位除法调用的时候，符号在自家目标文件里就有了着落。

内核镜像里另有一组 libc 拼写的内存函数，`memset`、`memcpy`、`memmove`、`memchr` 与 `memcmp` 的定义住在 `kernel/lib/mem.cpp`，那是内核世界的名单，走读归的是[内核库](../kernel/lib.md)。用户态的编译名单里今天没有它们的定义，链接这一关照常是过得去的，说明眼下的程序没有用上需要它们的样子。同一份 libc.cpp 也编进了内核镜像的名单，两个世界按各自的名单取用，咱们各认各的那一半。

start.S 还有一层没有写出的约定，它省掉了 .init_array 的轮转，全局的动态构造在用户态没有发生的时机。咱们看到的程序写法也确实照这个路数走了，smpstress 的共享页是零初始化的数组，常量的部分全部是 constexpr，没有谁依赖运行期的构造。这个立场与内核侧的零构造是同源的，用户态这边因为旗标关得干净（异常与 RTTI 都闭了，栈保护也关了），连那些桩都省了。

## 打印与格式化

freestanding 的这一侧没有 printf，打印是自组的。base 的格式化引擎给公共面的是 FormatToBuf，参数装进类型擦除的 Arg（一个 kind 加一个 64 位的 value），打包走的是 NumU、NumS、Str 与 Ptr 四个包装函数，视图由 ArgsView::of 在栈上的数组里数出来。smpstress 的 fail() 是消费的样例，咱们把它的写法摘一段：

```cpp
// user/programs/smpstress.cpp（fail() 节选）
char                           message[160]{};
const cinux::base::format::Arg kArgs[] = {
    cinux::base::format::Str(stage),
    cinux::base::format::NumU(round),
    cinux::base::format::NumS(status),
};
cinux::base::format::FormatToBuf(message, sizeof(message),
                                 "SMP_FORK_FAIL stage=%s round=%llu status=%lld\n",
                                 cinux::base::format::ArgsView::of(kArgs));
```

流程是这么走的，栈上开出的缓冲有 160 字节，stage、轮次与状态码包成了三个 Arg，FormatToBuf 按格式串把缓冲填满了。填完之后程序自己把缓冲一个字符一个字符地 `user::Write(1, ...)` 送到 1 号描述符，送到 NUL 的位置为止，因为引擎答的是 void，长度是它不报的，数写进去多少的活由 NUL 的判断代办了。成功的路径就干脆了，字面的字符串直接整串写出去。引擎认哪些转换符、截断的纪律是什么、数字转字符的活归谁，这些在[内核库](../kernel/lib.md)的格式化段落里都有，咱们不重念。

shell.cpp 走的是更朴素的路子，str_len、str_eq 与 put_text 三个小函数备在文件的头上，read_line 用 `user::Read(0, ...)` 逐字节地收，收到换行符了才收尾。一个命令行解释器要的字符串功夫，咱们点到的之外它还有 put_decimal 与 put_signed 两个手写数字打印的帮手，命令面与内置命令的走读归[Shell](shell.md)。

## glibc 的世界

盘上的另一界由 buildroot 装配，配方的家住在 user/rootfs/buildroot/。固定的版本住在 source.json 里，2026.08.1 的归档带着 SHA-256 的校验值，工具链选的是 Bootlin（提供预编译交叉工具链的厂商）的 x86-64 glibc 外部工具链，init 用的是 BusyBox，rootfs.tar 封的是 ext2 的开发盘，README 给的尺寸是 1536 MiB。用户表 users.txt 里记下的开发用户叫 cinux，UID 与 GID 给的都是 1000。配方与操作的细节 README 写得很全，咱们只挑与 libc 相关的认。

构建的装配名单咱们也看一眼，两界的产物送进的是同一张盘。`cmake/rootfs.cmake` 的 ext2 目标把一批用户 ELF 排进了依赖，名单里有 hello、shell、mapper 与各色的 demo，还有 smpstress 的身影。同排进依赖的还有 BusyBox、glibc 版的 hello，与静态链接的 vesper-desk（`third_party` 子模块出的桌面程序）。卷的归属咱们也交代一句：buildroot 的开发盘要是缺了，咱们靠启动目标把它补建出来，ext2 目标自己烤的夹具盘咱们不算进补建的名单。

glibc 侧最直白的证词是 ecotest。rootfs/busybox/ecotest.c 是一份普通的 C17 程序，include 的全是标准头，stdio、stdlib 加上 sys/shm、sys/socket 的这些，构建的旗标是 -std=gnu17，链接挂的选项是 `rpath /lib` 加动态链接器 `/lib64/ld-linux-x86-64.so.2`，产物落的是 build/glibc。它查的面有 System V 的共享内存（shmget、shmat 加父子进程的读写）、AF_UNIX 的流与数据报、poll、mmap 加 mprotect，--guest 参数打开之后查的还有挂载的生死周期。它能跑起来的前提，就是咱们前面对过的全部对上了，号码、寄存器、errno 与字节偏移的每一项都不差，glibc 的 wrapper 才一个字不用改。

动态链接在内核那一侧的对口是装载。exec 读程序头的时候看见 PT_INTERP，就把解释器的路径读出来、装进新的地址空间，入口的位置让给它，座位是 kernel/mm/layout.hpp 里的 kUserInterpreterBase（256 MiB），注释给的道理是确定性要紧，ASLR 是后面的问题。位置无关的主镜像的基址下限是 kUserExecImageBase（512 MiB），每次 exec 还叠一段随机的滑移，入口栈面上的 auxv（内核递给新程序的辅助向量）连 AT_RANDOM 的十六个字节都有出处，RDRAND 抽的，抽不出来的时候这一项干脆就不带了。装载的全过程[进程的复制与替换](../kernel/proc/fork_exec.md)讲过了，咱们不重念。

机内的原生开发环境是 glibc 世界的另一半。prepare.py 打包的是本机 x86-64 glibc 的开发环境，咱们数得过来的家当有 GCC、G++ 与 binutils，也有 CMake、Ninja、Make 与 Python 的名额，头文件与运行库一并合进了 overlay。README 还写明了这套工具不是可移植的发行包，换架构或者换 libc 的时候得另配工具链。overlay 把 Cinux 的平台文件装进了 CMake 的 Modules/Platform，独立的项目在机内配置的时候直接认得这套系统。user/examples/mini-primes 是机内开发环境的演示品，一个只依赖标准库的 C++17 小项目。机内验收的记录住在 buildroot 的 README（`user/rootfs/buildroot/README.md`）里，验收的是环境整体：空目录的全量构建、增量重编译与 GCC 的 hello 都过了，mini-primes 是随 overlay 进盘的示例之一。

musl（面向静态链接的轻量 libc 实现）的去处，两份文件头的注释对着读过就都交代了。桩文件的文件头说，musl 进门的时候自带 wrapper，那层桩到时候就退役了（this file retires），因为它对着的 ABI 已经是咱们自己的。内核侧的头文件在号码表的说明里写了同一层意思，静态链接的 musl 程序按这套号码原样发调用（原话是 `issues these numbers verbatim`）。

## 源码位置

- `user/start.S` — _start 入口与 UserMain 的交接
- `user/linker.ld` — 用户镜像 0x400000 的座位常量（当前构建未引用）
- `user/api/syscall.hpp` — ring 3 的系统调用桩与 Clone 的双边汇编
- `user/CMakeLists.txt` — 用户程序的旗标、链接选项与源文件名单
- `user/programs/hello.cpp` — 最小的用户程序样例
- `user/programs/shell.cpp` — 自带字符串小工具的 shell
- `user/programs/smpstress.cpp` — 消费 base 格式化引擎与 fork 压力的用户程序
- `user/programs/sigdemo.cpp`、`sigcatch.cpp` — 信号面的演示程序
- `kernel/syscall/syscall.hpp` — 两界共享的号码表、旗位与结构体布局
- `kernel/syscall/signal.hpp` — 信号号码、SigAction 布局与 SA_RESTORER 的约定
- `boot/early/libc.cpp` — strlen 与 __udivmoddi4 的最小 C 家当
- `base/include/cinux/format.hpp`、`base/src/format/` — 格式化引擎的公共面与编译单元
- `kernel/mm/layout.hpp` — 解释器座位与执行镜像基址的常量
- `kernel/loader/` — ELF 装载与 PT_INTERP 解释器的装载
- `user/rootfs/buildroot/README.md`、`source.json` — rootfs 的配方说明与固定版本
- `user/rootfs/buildroot/configs/cinux_defconfig`、`configs/users.txt` — 工具链选择与开发用户表
- `user/rootfs/busybox/ecotest.c` — glibc 动态链接的生态检查程序
- `user/examples/mini-primes/` — 机内原生编译的示例项目
- `scripts/glibc/build-hello-glibc.sh` — glibc 版 hello 的构建脚本
- `cmake/rootfs.cmake` — 两界产物进盘的装配名单
