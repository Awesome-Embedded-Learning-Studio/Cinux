# Terminal 终端

## 功能概述

这一页咱们按现在的源码把终端摊开讲清，它指的是三摊不同的东西，咱们一摊一摊认。控制台这半边也是内核的家当，管的是文本屏幕加行编辑的纪律。伪终端那半边同样是内核的，备的是桌面 shell 的座位，门牌有 `/dev/ptmx` 的，有 `/dev/pts` 的，还有 `/dev/tty` 的。桌面窗口那半边倒是整个住在用户态，vesper 的 PtySession 把 shell 拉在伪终端上，窗口里的 Terminal 控件把字节画成字符网格。

屏幕那一半的字形、光标与滚动，咱们在[视频子系统](../drivers/video.md)里已经走过一遍，这一页就不重念了，跟的是两次敲键的路径。头一次敲的是控制台，咱们看字节怎么编成行、^C 怎么变成信号。第二次敲的是桌面里的终端窗口，咱们看字节怎么穿过 PtySession 与伪终端，shell 的输出又怎么流回网格。沿途的取舍，咱们只评有出处的事，出处都在源码的注释里，正文标了地方。

## 控制台的扇出与让位

控制台是内核说话的中立面孔，console.hpp 的注释给它划的边界只有字符与字符串的进出，设备的名字它一概不认。`InitConsole` 的次序写在注释里，串口的开工排在头一位，为的是调试永远不等屏幕，屏幕则等引导记录里带着能用的帧缓冲才跟着起来。屏幕没起来的时候，控制台其实并不哑，串口独自把全部的输出扛走。PutChar 把字符交给每一条开口的路，底下的设备是谁，调用方自始至终是不知道的。器件那一半的细节，咱们在[串口驱动](../drivers/serial.md)与[视频子系统](../drivers/video.md)里都写过。屏幕还让着一块共享的显存，桌面握着 /dev/fb0 画图的时候，租约把控制台的笔画按下了，光标停在了原地，串口的那一半照常说话。桌面关掉最后一扇描述符的时候，屏幕才把像素收了回来。租约设备那一侧的细节，[Canvas 画布](../drivers/canvas.md)讲过了，咱们不重念。

## 行编辑的纪律：Tty

Tty 的定位写在 `kernel/driver/tty/tty.hpp` 的文件头里，说的是按键进、编好的行出，一个终端养一个自己的实例。这个类管的只是纪律，水管的进出一概不归它，字节的来路、回显的去处、等输入的读者怎么让路，都是装配的时候交进来的。TtyWiring 里坐着的是四个函数指针加一个上下文字。取键的叫 take_key，报信的叫 has_key，回显的叫 echo，让路的叫 wait，四个全是无捕获的函数指针。这个形状跟 FsOps 用的是同一种，FsOps 是每个文件后端交出来的操作函数表，表里的每一项都是一个函数指针配一个自家对象。注释把这么安排的用处讲清了，一套纪律类伺候的是许多终端，控制台接的是串口与键盘，伪终端的从端接的是主端的两只字节环，host 测试接的是假件，三种接法咱们往后的几节里都会挨个见到。

纪律是在 read 里跑的，注释给的缘由是编行的每一步都踩着读者拉字节的动作，字节一把一把地拉，行就一段一段地编，输入终究要等读者到场才跑得起来。咱们跟着一次按键走完它的路。可打印的字符在 ECHO 竖着的时候当场回显。退格擦字的时候，回显的是退格、空格、退格的三连，屏幕上的字真的消失，`\b` 与 0x7F 的两个码，认的都是退格。等到了行尾，整行连着换行符一起停进了字节池，等的是读者来取。ICANON 落下去的时候，字节是原样直通的，也不做编辑了。一行的编辑预算是 128 字节，NUL 也算在了里面，字节池的容量是 1024 字节，短读剩下的半行留在池里给下一个读者。写的那一半不归这里管，注释的原话是终端只拥有输入编辑。

termios 是终端的属性记录，纪律看的就是它，进与出走的是 copy_termios 与 apply_termios，整条记录原样地收发。TCGETS 与 TCSETS 这对问句在线上搬的是 36 字节的内核形状，注释的限定也落在这对问句上，搬的从来只有这 36 字节，termios2 的问句另算，按它自己的 44 字节走，glibc 的结构体比这大，tcgetattr 与 tcsetattr 干的是两种形状之间的换算。这段层次记在 `kernel/driver/tty/tty_config.hpp` 的注释里，它讲完层次还记了一回误量，2026 年 10 月 19 日量出 60 字节的那一版量的是 libc 的结构体，量错了层，多写的 24 字节越过了记录的边界，落进了调用者放在旁边的字段，咱们对了一下数，36 加 24 其实正是 60。

ISIG 竖着的时候，纪律其实认得三个控制字符，^C 折的是信号 2，^\ 折的是信号 3，^Z 折的是信号 20，咱们对照的常量是 tty_config.hpp 里的 kTtyIntrChar、kTtyQuitChar 与 kTtySuspChar，认的这件事独立于 ICANON，raw 模式下的信号键照样活着。信号号是停在 take_pending_signal 里的，拉键的读者顺手把它取走，投给了进程组。窗口的形状是终端另记的一项，管的就是行数、列数加两个像素位，在 Tty 里它与 termios 记录是平级的两个成员，ioctl 的问句答的就是它，set_window 换了形状，答出来的也跟着换了。

## ConsoleTty：控制台的装配与泵

ConsoleTty 是全内核装配好的唯一一台，咱们把它的座位对一遍。取键的座位按次序排了三把，串口坐的是头一把，console_tty.cpp 的注释给的缘由是 host 测试的世界在那儿打字，PS/2 键盘坐的是第二把，喂它的是 QEMU 的监视器，USB 键盘坐的是第三把。回显接的是 console::PutChar，字回出去的时候，串口与屏幕是同时看见的。让路接的是调度器的 yield，读者等行的时候，CPU 交给了别的工作。窗口的形状从文本屏幕的行列折出来。装配的时机是头一次用到的时候，constinit 的存储连构造步骤都省了，注释说装配好的时候引导横幅已经把屏幕铺好了，窗口折一次也就定下来了。shell 的 0 号描述符与 /dev/console 的打开者，摸到的都是同一台。

按键的收取不能光靠读者，console_node.cpp 里有一台专门的泵。StartConsoleInputPump 造一个名叫 `tty_poll` 的任务，权重填的是 200。咱们顺着循环看下去，活是轮着干的，poll_input 按一次至多 64 字节的定额把座位上的键收进纪律，take_pending_signal 把攒着的信号取出来，投给前台的进程组，然后是 yield 的让路。读者不在的时候，收键的活也有泵在做，^C 也就有了出口，这就是泵的用处。PS/2 键盘的字节怎么走到第二把座位上，[键盘驱动](../drivers/keyboard.md)讲过它的两条队列，控制台取的是字符那一脸。

## /dev/console：shell 的三条标准流

devfs 里的 console 节点是控制台的门牌，门牌的主从号是 `MakeDeviceId(5, 1)`，座位挂在虚拟的总线上。写的半边是简单的，write 把字节一个个地交给 PutChar。读的半边在 ConsoleInput 的一把自旋锁下进行，read_line 以不等待的姿势拉编好的行，顺手取走挂着的信号号，信号来了，投递走的就是前台的进程组，读者拿到的是 kInterrupted，没有行可给的时候答 WouldBlock，等不等的事，还是由系统调用那一层挑的。ioctl 认得的问句，咱们分两拨点。头一拨是 termios 的读与写，TCGETS 与 TCSETS 的四个词，加 termios2 的四个变号。另一拨答的是窗口、进程组与认主，TIOCGWINSZ 的窗口问答，TIOCGPGRP 与 TIOCSPGRP 的组问答，还有 TIOCSCTTY 的认主问句。问句表之外的答 ENOTTY。

前台进程组是一张单例的原子量，出生的值是 1，开机那个 shell 的组。TIOCSCTTY 认的是会话首，也就是 sid 等于 pid 的那个任务，名额是一次性的，占过了再来的答 EPERM，成了的时候调用者的组接管前台。用户进程要换人的话，走的是 TIOCSPGRP。控制台的窗口是只读的，问句表里有 TIOCGWINSZ 的读，没有 TIOCSWINSZ 的写，改窗口的权力在伪终端那边，咱们下一节看。

开机那一户的立门户在[Init 任务](../proc/init.md)里讲过了，跟终端有关的，咱们这里只对三行。seat_shell_task 把 shell 的 pgid 与 sid 都写成它自己的 pid（入册发的号是 1），controlling_tty 指向的是 `ConsoleTty()`，前台组由 AdoptConsoleForeground 交到了它的手里。三条标准流是 seat_console_streams 配的，file_world.cpp 把 /dev/console 解析了出来，开出来的 OpenFile（一次打开的描述对象）在文件表的 0、1、2 三个座位上各放了一份，shell 的读与写从此都落在了同一台身上。身份字是怎么发的，会话与进程组的边界在哪，更细的讲法在[任务的身份](../proc/pid.md)那一页里。

## 伪终端：给桌面 shell 备的座位

PTY 的本体在 `kernel/driver/tty/pty.hpp` 里，头文件的纲领说这是主从成对的一对，从端是一台真的终端，两只字节环只是它的水管，纪律长在从端的身上。两只环各有各的容量，raw 环 256 字节接主端写进来的键，主端环 4096 字节攒从端的输出与回显。从端的 Tty 在对子构造的时候就接好了线，取键的座位接的是 raw 环，回显的座位接的是主端环，让路的座位接的是构造时递进来的 hook，内核里是调度器的 yield，测试的世界里给的是空操作。穿过这一对的路有四条，咱们挨条念过去，主端写的字喂给纪律，从端的读者领走编好的行，从端自己写的输出推进主端环，主端的读者把从端产出的每一样都抽走，纪律的回显也在内。termios 是跟着从端走的，对从端应用的记录管的编辑，跟控制台的记录管 0 号描述符一模一样。

座位是开在 devfs 里的，管它的是 pty_node.cpp。打开 /dev/ptmx 的那一下，PtyTable 从座位表里领的是第一个空位，造一对新的 Pty，交出去的是主端的节点，从端的窗口尺寸从 ConsoleTty 接过一份，新对生下来就知道了屏幕的宽高。座位表一共备了 8 个位置，占满了之后，再来的每一对答的都是 ENOMEM。ENOMEM 这个答法看着是粗了一点，咱们对着注释一看才知道它的细处：空座位与活座位是分不开的，怪的是对子的寿命册子还没立起来，猜错的代价是旧从端把字节写进新主端的环里，所以在那之前老老实实答内存不足。ptmx 这个座位自己是读不得也写不得的，问它要字节答的是 EIO，它会的只有开一对新的。

从端的问句面比主端宽得多，宽窄咱们数过，termios 的读与写整批都认，TIOCGWINSZ 与 TIOCSWINSZ 也在认的名单里，改窗口的那一下多走一步，SIGWINCH 是按前台的进程组送出去的，行编辑拿它当的是重画的暗号。TIOCGPGRP 与 TIOCSPGRP 认的是座位自己的前台组，TIOCSCTTY 的规则跟控制台是同一个模子，认的也是会话首，名额也是一次性的。主端认的问句只有两个，TIOCGPTN 把对子的号报出去，TIOCSPTLCK 答的是一个 0。从端没行可给又没被挂断的时候答 WouldBlock，主端没货、从端还开着的时候，答的同样是 WouldBlock，从端写向一个关了的主端答 EIO，这些脸都长在 read 与 write 的返回值上。

挂断的线怎么走，咱们单独认一遍。主端最后一次独立打开关闭的时候，release_master 就把从端的纪律 hang_up 了，从端从此是读不到新键的，编辑到一半的半行排空了交给最后的读者。从端最后一次关闭的时候，主端的那一头看得见 EOF。两端的独立打开各自 pin 住自己的一头，dup 与 fork 借的是 OpenFile，共享的就是这些 pin。devfs 的面孔只答 WouldBlock，注释交代了等与不等由系统调用那一层挑。

/dev/tty 的解析也住在同一张表里，咱们跟着它走一遍。控制终端是 ConsoleTty 的任务，解析出来的是 devfs 的 console 节点，控制终端是某对从端的任务，解析出来的就是自己的 pts/N。shell 里跑的程序打开 /dev/tty，摸到的就是自己的控制终端，坐在哪一对的身上，外表是看不出来的。

## 桌面里的终端：PtySession 与 Terminal 网格

桌面那半边的第一块积木是 PtySession，vesper 的 `apps/terminal/host/pty.hpp` 里的一只薄壳，注释给它的定位是一个伪终端下的子进程加一个两头通用的描述符。open 走的是 libc 的 forkpty 加 execvpe，forkpty 是一步做完开伪终端、复制进程、把从端设成控制终端的那个函数，咱们在 host/pty.cpp 的注释里还读到一段旧事，它在 2.34 之前的老 glibc 里住在 libutil。子进程的落脚点是一台新的控制终端，shell 的行编辑因此照常运转，父进程握的是主端。主端在打开的时候就设成了非阻塞，读空了答 EAGAIN，绝不把调用方的一轮拖死。子进程退场的那一刻，EOF 或者 EIO 把 child_gone 翻了起来，之后的每一次调用都安静地答零。

尺寸与析构的事，咱们凑在这一段认。set_winsize 把列数与行数用 TIOCSWINSZ 报给了内核，内核转手就把 SIGWINCH 送给了子进程，行编辑拿它当的是重画的暗号。这个尺寸不给的话，行编辑按猜的宽度折行，提示符画在错位的行上，头文件的注释把这个后果写得具体。析构的头一件事是关主端，从端的控制终端跟着挂断，shell 收了 SIGHUP 走人，waitpid 收掉的是它的尸首，子进程是活不过窗口的。

窗口这一侧的网格，是 `apps/terminal/ui/terminal/terminal.hpp` 里的 Terminal 控件。格子的大小是从字体折出来的，用的是最宽的步进乘行高，边界是按整格裁的。两扇门的方向是相反的，on_key_down 干的是把键码折成字节交给源头，receive 把源头送回的字节落进网格。receive 认得的词有清单，可打印的 ASCII 落进光标下的格子，换行的活归 `\n`，回行头的活归 `\r`，退格擦字的活归 `\b`，八列停靠点的跳转归 `\t`。ANSI 转义序列（ESC 开头的控制串，终端里管清屏、挪光标、换颜色的那套词）认的是一个真提示符真会发的子集。认的词有擦行用的 CSI K，擦屏用的 CSI J，挪光标用的 CSI H，vt100 的四个方向键，还有 CSI m 的十六色 SGR，SGR 的调色板搬的是 xterm 的那套，粗体折成了加亮。其余的序列整个吞掉，词是解析了的，动作是没有的，屏幕上不会剩零散的括号。打满了末列就折行，末行上换行的话，整屏卷上去的就是一行。画出来的姿态是反色的匣子，地是亮的，字与光标是挖出来的洞，咱们在屏幕上看见的字就是这些洞。

源头咱们分两路看。真的一路是 PtySource，send 干的活是把字节写进主端，pump 等的是 master 有货，抽回来的货按 256 字节一段喂给了网格，子进程走了，屏幕补上的就是一行 (exited)，补的次数只有一次，pump 的等待就是事件循环的睡眠。假的一路是 LoopbackSource，发回去的字原样地回来，注释说网格这一侧是照着同一条接缝造出来再验收的，伪终端不过是同一条缝上换了真程序。

fbdev 桌面接的是真的 PtySource，接的线都在 fbdev_desktop.cpp 里，里头的 WindowFrame 标着 Terminal，内容区坐的就是这个网格。shell 的拉起用的是下面的这组参数，咱们从源码里原样搬下来的，程序给的是 /bin/sh，参数里带的是 -il，环境字带的是 TERM=vt100 与 `PS1=> `。

```cpp
// third_party/vesper/examples/fbdev_desktop.cpp
constexpr std::array<const char*, 2> sh_args{"sh", "-il"};
constexpr std::array<const char*, 2> sh_env{"TERM=vt100", "PS1=> "};
auto sh_session = vesper::host::PtySession::open("/bin/sh", sh_args, sh_env);
```

注释的话咱们对着源文件核过，两个环境字的用处都交代了，TERM=vt100 是给 clear 的转义词用的，哑终端是连 clear 都发不出的，PS1 的值定成了光秃秃的 `PS1=> `，字体是画得出来的。开跑之前 set_winsize 把网格自己的行列数报过去，行编辑折的就是屏幕的宽度。主循环排进 poll 的描述符有三张，键盘的、指点的、主端的，事件交给 visor 的 InputRouter 落进焦点窗口，主端醒的时候，shell 的输出就跟着泵了出来。shell 死了之后它的槽位从 poll 里撤走，负的 fd 会被忽略，注释说不然每一个节拍都醒在同一根挂断的线上。

vesper 另有一个独立的终端程序 vesper_terminal，咱们把它跟 fbdev 的例子放在一起比，差异就出来了。它走的是 Wayland 的路，默认拉起的程序是 bash，缩放的要求来的时候，网格重裁了，winsize 也重报了，程序退出后的那一刻，告别的一行上了屏，最后一帧提交了，窗口也跟着走了。x11 与 wayland 的两个桌面例子同样用 PtySession 拉 bash，pty_tests.cpp 的测试在开发机上跑，拿的是 /bin/cat 与 /bin/echo，驱动的是整条链。这些座位底下是内核备好的门牌与问句面，ptmx、pts 与 tty 说的都是 Linux 的词，为的就是 libc 那套 tcgetattr、forkpty 原样跑通。

## 源码位置

- `kernel/console/console.hpp/cpp` — 控制台的中立面孔与扇出
- `kernel/console/screen.hpp/cpp` — TextConsole 与让位租约的屏幕半边，走读在[视频子系统](../drivers/video.md)
- `kernel/driver/tty/tty.hpp/cpp`、`kernel/driver/tty/tty_config.hpp` — Tty 行编辑纪律、termios 问答与各档预算
- `kernel/driver/tty/console_tty.hpp/cpp` — ConsoleTty 的装配与三个取键座位
- `kernel/driver/tty/pty.hpp/cpp`、`kernel/driver/tty/pty_config.hpp` — 伪终端对子、两只字节环与座位表常量
- `kernel/fs/devfs/console_node.cpp` — /dev/console、`tty_poll` 泵、前台进程组与 ioctl 面
- `kernel/fs/devfs/pty_node.cpp` — ptmx、pts/N、/dev/tty 的座位、解析与 ioctl 面
- `kernel/fs/file_world.cpp` — seat_console_streams 配的三条标准流
- `kernel/init/init_thread.cpp` — seat_shell_task 的控制终端与前台立户
- `third_party/vesper/apps/terminal/host/pty.hpp/cpp` — PtySession
- `third_party/vesper/apps/terminal/ui/terminal/terminal.hpp/cpp` — Terminal 网格控件、转义解析与 LoopbackSource 假件
- `third_party/vesper/apps/terminal/ui/pty_source/pty_source.hpp/cpp` — PtySource 的 send 与 pump
- `third_party/vesper/apps/terminal/app/terminal_app.cpp` — vesper_terminal 独立终端程序
- `third_party/vesper/examples/fbdev_desktop.cpp` — 桌面例子的终端窗口与三路 poll 主循环
- `third_party/vesper/tests/apps/pty_tests.cpp` — 开发机上的伪终端测试
