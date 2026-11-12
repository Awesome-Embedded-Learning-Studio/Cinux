# Shell：用户态的命令行

## 功能概述

这一页咱们把 Cinux 自带的命令行解释器完整地走一遍。它的本体是 `user/programs/shell.cpp`，一个不带 C 库的独立程序（编译的时候不带 C 库与 C 启动文件，系统调用全由咱们自己发），编出来的 `shell.elf` 能不能坐上 `/bin/sh` 这个座位，烤盘说了算：busybox 在场的烤法座位归 busybox 的 ash，咱们编的 `shell.elf` 根本进不了卷，busybox 缺席的烤法才轮到它，而这样的烤法只住在 musl 小盘路上，默认的 glibc 烤法把 busybox 备齐了才动工（烤盘就是把根卷镜像烤出来的那一步，具体的做法归[构建脚本](../scripts.md)那一页管）。开机之后教学卷的控制台上坐的是 ash，教学 shell 的横幅要它自己真坐上 `/bin/sh` 才打得出来。它自己的本事咱们照旧一件件认：内建的就地办掉，文件类的转给系统调用，`run` 负责把别的程序拉起来跑。

咱们把范围也得交代清楚。行编辑的纪律（回显、退格、^C 怎么折成信号）是内核终端的家当，[Terminal 终端](../kernel/gui/terminal.md)按条文讲过了，咱们这一页只认结果：shell 拿到的已经是编好的行。fork、execve、wait 在内核里是怎么走的，[进程的复制与替换：fork、execve 与 wait](../kernel/proc/fork_exec.md)讲的是内核那一侧。打开的文件在内核里怎么变成一张打开描述，要看它的话，咱们得走到[文件与路径](../kernel/fs/file_and_path.md)那一页。桌面窗口那半个世界（vesper 的 PtySession 把 shell 拉在伪终端上，窗口里的控件把字节画成字符网格）同样在 Terminal 终端页。咱们这一页守着 shell 程序自己。

## 谁把它拉起来

它身后有一条接力的链，咱们一棒一棒看，shell 是被拉起来的，自己是不会醒的。头一棒是内核的：kernel_init 这个内核任务把根卷挂好了之后，去卷上把 `/sbin/init` 这个文件读了出来。

咱们接着看下一步：kernel_init 把读出来的 ELF 装进一个全新的地址空间，新进程领了 1 号 pid。

身份的接线咱们也数一遍。三条标准流（0、1、2）全部接在了 `/dev/console` 上，控制终端则指向了 ConsoleTty，进程组与会话都落在它自己的 pid 上。

这些活住的正是 `kernel/init/init_thread.cpp`，细节咱们在[Init 任务](../kernel/proc/init.md)里已经走过一遍，这里咱们只借结果：1 号进程跑的是 busybox 的 init。busybox 是把几十个常用 Unix 工具装进一个可执行文件的小工具箱，`/sbin/init` 是它的一个别名。

第二棒落在了 busybox init 的身上，它读的是 `/etc/inittab`，照着里面的记载决定拉什么。教学盘的种子 inittab（`scripts/ext2_seed/inittab`），咱们看到的就两行：

```
::sysinit:/bin/echo Cinux up
console::respawn:/bin/sh
```

冒号把一行分成了四段，末段是要跑的命令，respawn 是动作段的名字，意思是命令退出了就再拉一次。头一行开机的时候打一句 Cinux up，第二行才是咱们要认的正座：在控制台上跑的是 `/bin/sh`。种子喂的是教学卷。日常开发默认用的是 Buildroot 卷，它跟教学卷是分开烤的两卷，两边的配置互不通用。教学卷在[构建脚本](../scripts.md)那一页的名字是生态卷，说的是同一份烤法烤出来的默认产物，同一份烤法里还带着一条 `CINUX_LIBC=musl` 的 16 MiB 小盘路，仓库里另外还有一张装整份源码的自编译卷，卷的清单归那一页讲，咱们这里不铺开。Buildroot 卷的 `/etc/inittab` 来自 `user/rootfs/buildroot/overlay/etc/inittab`，桌面那行走的是 `::once` 的门控（只拉一次），控制台 respawn 的换成了 `/bin/su - cinux`，咱们这一页讲的 respawn 直拉 `/bin/sh` 是教学卷才有的排法。

`/bin/sh` 这个名字底下坐的是谁，烤盘的脚本说了算。`scripts/create_ext2_disk.sh` 认的是两条烤路，默认的 glibc 路与 `CINUX_LIBC=musl` 的 16 MiB 小盘路，那个 if 加一个 elif 的座位安排只住在小盘路上。小盘路上 busybox 在场的话，它就把 busybox 硬链到了一排工具名上，`sh` 的名字在名单里，`/sbin/init` 也是 busybox 的别名，座位就归了 busybox 的 ash，ash 是 busybox 自带的 shell。busybox 缺席的话，它才把咱们编的 `shell.elf`（构建系统用变量 `CINUX_SHELL_ELF` 指着的那个产物）写进 `/bin/sh`，打印的那句话自己都招了，说的是 teaching shell seated，括号里注明的是 no busybox source tree。默认的 glibc 路没有这个顶替的分支，busybox 是备齐了才放行的，缺了它脚本当场报错退场，盘也就烤不出来了。所以咱们的 shell 程序是教学烤法的兜底正座，只是这个座位安在了 musl 小盘路上，平时的正座是 ash 的，咱们这个程序是替补，烤盘的时候 busybox 不在了，顶上去的就是它。

第三棒又回到了内核收尸。kernel_init 守着收尸的循环，`wait_child` 是停下来等 1 号的，收到了退出就往串口打一行 respawn，回到循环的头顶，把 `/sbin/init` 又拉了一遍。串口上咱们能看到 `shell exited with 0, respawning` 这样的字样。shell 自己退了，链条又把它转了回来，循环是不停的，控制台上的提示符就总有回来的一天。

## 程序本体：一个不带库的循环

`user/CMakeLists.txt` 里管它的是 `user_shell` 这个目标，编译旗子把 C 的世界挡在门外：`-nostdlib` 是不给运行时的意思，链接选项 `-Wl,-e,_start` 则把入口指到了 `user/start.S`。这句说得再准一点：那个 `-nostdlib` 其实住在链接选项里，与 `-Wl,-e,_start` 这个入口选项挂的是同一行，它挡掉的是 C 库与 C 启动文件，编译器自家的 libgcc 支撑库也还挂在链接行上，少了它，碰到编译器生成的支撑调用就链接不上。start.S 是短的，咱们整个贴出来：

```asm
.section .text.start,"ax"
.global _start
.type _start, @function
_start:
    xorq %rbp, %rbp
    call UserMain
    movq $60, %rax
    xorq %rdi, %rdi
    syscall
    ud2
.size _start, .-_start
.section .note.GNU-stack,"",@progbits
```

开头的活是清掉 rbp，然后一路跳进了 UserMain。咱们接着读，UserMain 要是真返回了（它实际上是回不来的，讲 exit 的时候就明白），汇编就得补一发 60 号的系统调用，号码对应的就是 exit(0)，末尾的 ud2 是一条故意触发非法指令异常的指令，用户态的程序真踩上去，接住它的只有内核的兜底路：异常号与寄存器的现场报在串口上，转储完了才收场，踩中的那颗 CPU 从此停在了内核里。而进程收不到信号，收尸与 respawn 的环节也都轮不上它，ud2 算是给跑飞的指令流留的拦截线。

库是不带的，日用的家什全得咱们自己打：`str_len` 与 `str_eq` 是 strlen 与 strcmp 的替身，`put_decimal` 把无符号数一位一位地取出来，再倒着写了出去，负数另备了 `put_signed`。系统调用这一侧倒是不用造轮子，`user/api/syscall.hpp` 把每个调用都包成了同名的函数，里头发的就是一条 `syscall` 指令。号码与寄存器的约定跟 Linux x86_64 对齐，错误按负的 errno 回来，[系统调用](../kernel/syscall.md)那一页讲过它的底细。

UserMain 的骨架值得咱们整段贴出来，shell 的全部生活就在这几行里：

```cpp
extern "C" void UserMain() {
    put_text("cinux shell - type 'help'\n");
    char  line[kMaxLine];
    char* words[kMaxWords];
    for (;;) {
        put_text("cinux> ");
        if (read_line(line) == 0) {
            continue;
        }
        const unsigned long kCount = tokenize(line, words);
        if (kCount > 0) {
            dispatch(kCount, words);
        }
    }
}
```

横幅是打一次的，然后一圈一圈地转下去，转的就是提示符、读行、空行重转、切词、分发这一套。咱们再数两个常量，它们框住了全部的胃口：`kMaxLine` 定的是 128，一行至多收的是 127 个字符，外带一个收尾的 \0。`kMaxWords` 定的是 16，一句至多认的也是 16 个词。

## 一行输入怎么读

`read_line` 的胃口更小，一次 `Read(0, &glyph, 1)` 要的只是一个字节。读到了就往 `line` 里放，换行符来了收尾，补上收尾的 \0，长度也就交回去了。`Read` 没有正好拿回一个字节的话，它就 `continue` 回头再试就是了：控制台那儿还没有整行的时候，内核答的是暂无，shell 拿自己的循环当轮询使。EOF 与出错咱们在这里不加分辩，一律按再试处理就是了。这是它的态度，标准输入是永远开着的，行是迟早会来的。

回显是不归它管的。咱们敲键时屏幕上一个个冒出来的字，是内核终端纪律的手笔：Tty 在内核侧做的是编行、回显、擦字，攒齐一整行才交给读者。一行的预算是 128 字节，内核那边与 shell 这边认的是同一个数，咱们这里认领结果就好，`read_line` 拿到的已经是编好的行。

## 切词与分发

`tokenize` 的刀落在原缓冲区上，当刀口的是空格与制表符：词头的地址记下来，词尾的字节改写成 \0，`words` 数组收的全是指向原缓冲区的指针。封顶的是 16 个词，排满了就不收。引号是不认的，转义也是不认的，变量展开就更没有了。咱们敲 `echo hello world`，它数出来的是两个词，带空格的文件名它接不住，这个粗法咱们如实记下。

`dispatch` 干的是一排字符串比较。`str_eq` 把 words 的头一个（也就是惯例里说的 argv[0]）逐个地比，比中了 echo 就走 echo，比中了 ls 就走 ls，一路排了下去，到了 poweroff 打住。全都落了空，打一行 `unknown command` 就完了。`help` 是它的自我介绍，咱们把清单录在下面：

```
commands: echo [text...] [> file], ls [dir], cat <file>, stat <path>, touch <file>, mkdir <dir>, rm <path>, cd [dir], pwd, run <prog>, help, yield, exit, poweroff
```

## 内建命令逐个认

help 清单上的十四个名字，咱们在这里认的主体是十个。help 前一节已经自我介绍过了，run 有自己的一节，exit 与 poweroff 归后面讲退出与再拉起的那部分，到那里咱们接着认。

echo 的正职是把词按空格连起来打一行，顺手兼着全 shell 唯一的重定向：词流里要是冒出了 `>`，而且后面跟着的是文件名，它就用 `Open(path, kOpenCreat)` 给咱们开了一个文件，把前面的词写进去，行尾补的是换行符，然后一句 `Close` 收了尾。`>` 后面不跟文件名的话，重定向是不成立的，`>` 就按普通词原样打出来了。别的命令都是不认 `>` 的，咱们想把目录清单存进文件的话，这个 shell 里是没有路的。

ls 读目录用的是 `Getdents`：参数空的时候就落到根，`or_root` 这个小函数干的就是把空的参数换成 `/`，目录是打开了的，一次要 512 字节的目录项，按每项自带的长度字段迈步，名字打出来的就是一行，目录类型的尾巴上补个斜杠。目录项的内存布局是内核与 shell 之间的约定，偏移常量的家在 `kernel/syscall/syscall.hpp`。打开的文件在内核里怎么落座成一张打开描述，文件与路径页是讲过的，咱们不重念。

咱们看 cat，它开了文件就 128 字节一勺一勺舀，`Read` 拿回来的数不比 0 大就停手，0 是到头了，负数是出了错，舀出来的全交给了标准输出。touch 做的只有 `Open` 加 `Close`，靠 `kOpenCreat` 的帮忙，文件不在的话就造一个，在了就原样放着。mkdir 与 rm 各自是一行调用的活：`Mkdir` 与 `Unlink`，失败照报错的惯例走。

咱们要换目录，靠的是 `Chdir`，cd 把它包了一层，不带参数的时候回的是根。pwd 靠的是 `Getcwd`，把当前的路径领出来打一行。stat 是最实在的，`SyscallStat` 拿回来的有五个数：inode、mode、链接数、大小、修改时间，打的时候一个一个上，写的是十进制，翻译是不做的。

咱们挨了错误的时候，文件类的内建报法走的是同一条路：`report_errno` 拼的是调用名、`failed: errno` 与数字，拼成的是一行，打出来比如是 `open failed: errno 2` 的样子，数字是内核答回来的负 errno 的绝对值。例外的也有，`run` 的三条失败路不进这套报法，各自带着 `run:` 开头的话自己报，后文讲到 run 的时候再认。errno 是系统调用报错的老惯例，出错的种类编成了号，按负数送了回去，这个约定系统调用页是讲过的。

yield 是最闲的，咱们敲下去，`Yield()` 把 CPU 让出去了，转了一圈回来，打的是 `yielded and back`，算是给别的任务腾了一步路，自己也不耽误自己的事。

## run：把别的程序拉起来

咱们跑别的程序，靠的是 `run <程序路径>`，这是 shell 唯一的外出通道，`run_program` 干的是三步。`Fork` 了一回，分出来的是两家，咱们跟着孩子这一边走：它发起的是 `Execve(path)`，成了就不回来，整个世界就换成了目标程序，`user/api/syscall.hpp` 的注释原话是 replace this world with a program，这画面就是照着注释译过来的。

孩子这一边也有不成的时候，咱们把失败的一路单独看：路径是不对的、ELF 是装不上的，打出来的就是 `run: exec refused`，孩子自己是由 `Exit(1)` 交出去的。

孩子这一路咱们走完了，父亲这一边咱们接着跟：它靠的是 `Wait4(-1, &code)` 停下来等，收着谁的尸就报谁的号，比如打出来的就是 `[3] exited with 0`。孩子挨了信号的话，报法换的是 `[3] killed by signal 2`。

报号里的数字是从状态字里读出来的，读法咱们对的是 Linux 的 wait 语义：低 8 位非零的时候是信号号，不然的话高 8 位就是退出码。

参数这一头咱们也交代一句：`run` 后面忘了给路径，它提醒的正是 `run: need a program path`。fork 或 wait 本身要是被拒了，数字是原样带出来的。

fork、execve、wait 在内核里的全程，进程的复制与替换那一页走过，咱们这里只认 shell 这一层的用法。

有一件事咱们得当面讲清，参数是传不进去的。`Execve` 的封装只收一个路径，`user/api/syscall.hpp` 里它的签名就一个 `const char*`，后两个槽位填的是 0。`run` 后面的词只消费头一个，想给 hello 递个参数的话，这版 shell 是办不到的。程序之间要配合的话，眼下能走的是文件与控制台。

^C 的故事里 shell 什么都没干，咱们借它把线连起来看。shell 里跑一个死循环的程序，^C 拍下去的时候，程序就死了，shell 醒来打的就是 `[4] killed by signal 2`。终端纪律是认得 ^C 的，把它折成了信号 2，投给前台的进程组，跑着的孩子在组里，接了信号就退，`Wait4` 醒来报的也是实情。前台组与会话的身份字，咱们要细究的是[任务的身份：pid、tgid、进程组与会话](../kernel/proc/pid.md)，那里讲的是怎么发的。^C 的折法在 Terminal 终端页。

## 退出与再拉起

`exit` 是最短的命令，`Exit(0)` 发了出去，shell 的世界就收了。收了并不散场：busybox init 的 respawn 把 `/bin/sh` 再拉起来，上游 kernel_init 的收尸循环也守在链条的顶端。咱们在控制台上敲 exit，屏幕是停一停的，提示符就回来了，靠的是 respawn 机制本身。回来的座位上坐的还是 ash，教学卷平时当班的就是它。教学 shell 的横幅只有它自己坐上 `/bin/sh` 才打得出来，可眼下 busybox 缺席的烤法连 `/sbin/init` 都不装，内核也就读不到 init 了，打出来的就是 `shell refused, parking`，什么用户进程都不拉了，它的横幅咱们在开机路上是见不到的。想让 shell 彻底下岗的话，得动的是 init 那一层，这活是轮不到 shell 自己的。

poweroff 走的是关机的门，咱们敲下去，它打出来的是 `cutting power through acpi`，跟着发的是 `Reboot()`，参数里带着关机用的魔数（约定好的固定值，内核是认到了才动手的）。真关掉了，这行代码是回不来的。`Reboot()` 要是带了个负数回来（内核那一侧没接茬），它就按报错的惯例补一行 `poweroff failed: errno N`，跟 mkdir 与 rm 用的是同一个报法。固件收了睡眠字却纹丝不动的话，报错行是不出的，shell 是还活着的，咱们接着看到的就是这句自白：

```
machine still standing; firmware ignored the sleep word
```

咱们要是还能看见这句，说明的是机器还站着。

窗口的那一头是另一条线：桌面终端是把 shell 拉在伪终端上的，窗口关掉、主端关闭的时候从端怎么挂断，vesper 的 PtySession 怎么接线，Terminal 终端页都讲了，咱们不抢它的活。

## 源码位置

- `user/programs/shell.cpp` — shell 程序本体：读行、切词、内建命令与 run 的 fork/exec 全在这里
- `user/start.S` — 用户程序的入口 _start，清 rbp 调 UserMain，返回就补一发 exit
- `user/api/syscall.hpp` — shell 用到的全部系统调用封装，Execve 只收路径的签名在这里
- `user/CMakeLists.txt` — user_shell 目标的编译与链接，-nostdlib 与入口符号的出处
- `cmake/rootfs.cmake` — 把 shell.elf 的构建产物作为 CINUX_SHELL_ELF 传给烤盘脚本
- `scripts/create_ext2_disk.sh` — 烤盘脚本，busybox 与教学 shell 谁坐 /bin/sh 由它定
- `scripts/ext2_seed/inittab` — busybox init 读的种子配置，console::respawn:/bin/sh 在这里
- `kernel/init/init_thread.cpp` — kernel_init 拉 /sbin/init 与收尸 respawn 的循环
