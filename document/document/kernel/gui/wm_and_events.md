# 窗口管理器与事件系统

## 功能概述

这一页讲的是两桩事，我们分开来说：旧窗口管理器与事件系统是怎么退场的，还有现在的事件链路是怎么走的。窗口、Z-order、事件循环这些桌面的活，如今已经全搬去了用户态，桌面住的是 vesper（`third_party/vesper` 这个 submodule）里 `vesper::desktop` 的控件树。内核留下的只有输入事件流：驱动把事件送进各自的流，devfs 摆出来的文件名是 /dev/event0 与 /dev/event1，桌面从那里领走自己的事件。旧世界的退场是分两波走的，日子我们记在这里：2026 年 7 月 5 日的 `dd0e18f` 删掉了内核里的老桌面层，10 月 31 日的 `49ddfb0` 清场，`kernel/gui` 的剩余部分、用户态库 `libs/gui` 加 CINUX_GUI 编译开关，都在这笔提交里退了场。

分工我们在这里画清。驱动怎么生产事件（扫描码、三字节包、座位怎么落）的事，归 [键盘驱动](../drivers/keyboard.md) 与 [指点设备](../drivers/mouse.md) 两页讲过了，绘图与 /dev/fb0 的座位归 [Canvas 画布](../drivers/canvas.md)，本页管的是事件离开设备节点之后的路：内核的 EventStream（事件流，一台输入设备自己的事件队列）怎么接住，独占的问话怎么答，桌面那头的 vesper 怎么消费，visor 的主循环节奏是什么，它们都是本页要讲的内容。退役记的体裁跟 [小内核](../mini.md) 那一页一样，事实从 git 的历史里对出来，旧文件的原文用 `git show 49ddfb0^:路径` 这样的命令随时调得出来。

## 退役记：两波退场

第一波发生在 2026 年 7 月 5 日的 `dd0e18f`，提交说明是 `feat(gui): F13-B 切新 Cinux-GUI core(P0-P7)+ 删老桌面层/老测试` 这么一句。`kernel/gui` 里的 `window_manager.cpp/hpp`、`window.cpp/hpp`、`terminal.cpp/hpp`、`terminal_ansi.cpp`、`desktop_icon.hpp`、`icon.hpp` 这一批桌面层文件在这一波里删掉了，提交说明把这批名字点了出来，加起来有 1500 行上下的样子。真正的内核窗口管理器是这个世界，我们把它画一下：WindowManager 画的是背景，按 Z-order 排的是窗口，顶上叠的是一个软件的光标。指针输入的套路是 press capture 加点一下抬到最前，头文件的注释管它叫 click-to-raise，`kMaxWindows` 定的是 16 个窗口的位子。桌面在这一波里切到了用户态库 Cinux-GUI core，它当时是 third_party 的 cinux-gui 子模块，`89392a1`（2026-08-23 并回主仓的那笔）把子模块并回了主仓，后来住的地方是 `libs/gui`。

第二波是 2026 年 10 月 31 日的 `49ddfb0`，提交说明还是 `feat: Migrate the refator version of Cinux` 这原样的一句，动机在文字里是找不到的。这笔提交删掉的东西我们分三摊来看。`kernel/gui` 剩下的 8 个路径全没了，我们一个个念：`event.hpp/cpp` 是旧的事件系统，`gui_init.cpp/hpp` 是启动桩的两半，`desktop_launch.cpp` 管的是用户态的启动，`data/icon_data.hpp` 装的是图标的位图，`CMakeLists.txt` 是它们的构建清单。`libs/gui` 是整库退场的，140 个文件一个都不剩了。WindowManager 的第二代、Compositor 合成器这样的角色，连同各自的控件与测试，都装进这个库里了。画布的两半（`kernel/drivers/canvas.hpp/cpp`）与 CINUX_GUI 开关也在名单上，开关原来的家在 `cmake/options.cmake` 与 CI 工作流里，如今全树搜它的残留，一处都搜不到了。画布的退役 [Canvas 画布] 一页记过了，这里点的只是目录的名字。

旧的事件系统长什么样，我们拿 `git show 49ddfb0^:kernel/gui/event.hpp` 回看。它的命名空间里有三样东西。EventType 枚举的取值有五种，把鼠标的移动、按下、抬起与键盘的按下、抬起全装下了，Event 联合体的判别字是 `type_`，装的是 MouseEvent 或者 KeyEvent，EventQueue 则是单生产者单消费者的环形缓冲。`BUF_SIZE` 定的是 128 条，队列满了的时候，新来的事件无声地消失。注释把分工交代得清清楚楚的，说 IRQ 的处理器是生产的那一头，窗口管理器是消费的那一头，它 poll 的就是同一个队列里的全部输入。它跟现在的 EventStream 是两种动物：旧队列装的是内核自定义的 Event，新流装的是 Linux evdev 形状的 24 字节记录，形状的事 [键盘驱动] 讲过了。

事件系统退场之前的那几个月，`kernel/gui` 的活已经缩过一圈了，我们把这段铺垫的三个提交排一下。`dd0e18f` 之后桌面控件搬去了用户态库。`45378a1`（2026-07-06 的批 2）把 /dev/event0 的输入送到用户态，鼠标与键盘的记录双写进同一条流。`62ae31d`（同一天的批 3b）用 fork+execve 拉起 `user/cinux_gui_host`，替掉了内核里的 gui_worker 线程。`desktop_launch.cpp` 的文件头注释把当时的安排写得很全：内核的 `gui_start` 只负责武装 PS/2 鼠标与键盘的监听器，把解码好的事件双写进 /dev/event0，控件树与 GuiCore 全在用户态的 host 里搭，`handoff_framebuffer_to_gui` 的活轻了，不再搭任何内核的控件树，摘掉的只是文字控制台。这套安排跟现状的距离只剩一步：双写的监听器与启动桩整个退役，驱动直接往设备自己的流里进事件，读的活归桌面自己做。

旧世界的活去了哪，我们一摊一摊对过去。旧 host 干过的三样活如今归 vesper 的 `fbdev_desktop` 例子接手。它开的是 /dev/fb0 与 /dev/event0 这样两个文件，搭起自己的控件树，跑自己的事件循环。读循环的形状、EVIOCGRAB 的姿态、SYN_DROPPED 的重建这三样有明文的继承条，`evdev_input.hpp` 的文件头注释写着 Adapted from Cinux-GUI@0b0c135，说读循环、独占与重建是那个仓库的，累加器换成了 visor 的翻译器。窗口与控件的活归 `vesper::desktop` 的 Desktop、WindowFrame、Terminal，[Canvas 画布] 已经把桌面树交代过了。

## 内核侧：EventStream 怎么接住事件

EventStream 的家在 `kernel/driver/input/event_stream.hpp/cpp`，一台设备配一条自己的流。文件头注释给它的定位是事件花名册：词是从 IRQ 递进来的，读者领走的是整条的记录，独占的那个字决定还有谁能听见。生产的一侧是 publish，注释对它的要求是在中断上下文里也安全，publish 干的活是往队里放一条记录，做的全是整数运算，里面没有一条是拖节奏的动作。每条记录进门的时候打上自己的时间戳，秒与微秒从 Tick 的节拍换算出来，形状就是 Linux `struct input_event` 的 24 字节。队列是 64 条的环形，`input_config.hpp` 的注释替队列表明了立场，丢掉的爆发只算一个错过的手势，到货的记录一句也不虚报。

溢出的处理我们把顺序走一遍，这一段是 [键盘驱动] 没有展开的。队列满了的时候 publish 把整条队列清空，把 `dropped_` 立成丢了东西的标志，然后把当前的记录放进空出来的队列。下一次 read 领到的头一条记录是 SYN_DROPPED（EV_SYN 里 code 3 的那个词），它把丢过东西的消息报给读者。接着流按 `delivered_keys_` 的花名册把已经送出去的按键逐个补发释放，绝对轴的设备再把两根轴的当前位置重放一遍，末尾的一条 SYN_REPORT 把报告合上。读的人拿到的是一份自洽的状态，按住的键不会悬在半空。用户态那头对同一个词的反应是重置整份翻译状态，这个反应的现场归后面 vesper 的段落讲。同一段路的两头各有各的做法，内核补发记录、用户态清掉自己的翻译状态。

ioctl 的问句，流认得的有三类，answer 把它们一个个地对过去。EVIOCGKEY 答当前的按键位图，流跟踪的事件码有 0x300 个，一码一位、八码一字节地摊开。EVIOCGABS 答绝对轴的量程与当前位置。EVIOCGRAB 管的则是独占，规则的次序源码注释排得很清：一次独立的 open 持有一份独占，已有的独占连自己再要一次也答 EBUSY，别人来放的话答案是 EINVAL，最后一个描述符关闭的时候 `release_owner` 把独占放掉、顺手唤起 grab_hook。grab_hook 是接线注册的函数指针，键盘驱动在 `attach()` 里装的是 `Keyboard::grab_hook`，独占的消息一来，它就把键盘的字形开关翻成假，控制台那一路就收不到键了，这段现场归 [键盘驱动] 的读者小节讲。流还留着 `poll_hook` 的座位，报告要等抽干环形队列的设备（数位板那一路）把自己的 drain 交过来，read 与 poll 进门的时候都会跑它，不来中断的读者也拿得到报告。

devfs 的这一层很薄，我们把座位的落点也看了：座位落在 `kernel/fs/devfs/input_node.cpp`，event0 坐的是从号 64，event1 坐的是从号 65，主号 13 是 Linux 的 evdev 用的那个数（`kInputEventMajor` 的注释原话）。每份 open 对应一个引用计数管理的 InputOpen，析构的时候 `release_owner` 放独占，read、has_input、ioctl 三个动词做的事都是原样转发给流，write 的回答是不支持。座位的落法、event1 在数位板与 PS/2 鼠标之间的挑法，[键盘驱动] 与 [指点设备] 两页都写过了，这里就不再重复了。

## 用户态：vesper 怎么消费

桌面那头的一台设备对应一个 EvdevInput（`third_party/vesper/src/host/evdev_input.hpp/cpp`）。open 拿 `O_RDONLY | O_NONBLOCK` 这样的旗子开节点，紧跟着发出去的问话是 EVIOCGRAB，独占的姿态是尽力而为，已被别人抓着的设备照读不误，绝对轴的设备再用 EVIOCGABS 把两根轴的真实量程读出来。量程折算的细节 [键盘驱动] 已经讲过了，剩下的两件事我们补在这里。头一件活是开机的种子：内核不必在 open 之后重复没变的轴，vesper 把读到的两份当前位置各喂一次翻译器，注释的说法是静止的一次点击也要带着设备的坐标。另一件是翻译器的分配，键盘的状态装修饰键，指针的状态装位置，每个设备各配各的，头文件注释的理由是两台设备合成一个状态的话，数位板的词会把按住的 Shift 冲掉。

poll_next 的排空节奏我们跟着读。它是非阻塞的，一条一条 `input_event` 地领，读不满一整条的时候它就带着半截的状态返回，剩下的下一轮接着处理。指针按钮的词攒在按钮的队列里，等的是 SYN_REPORT 再结算，命中测试要用报告收尾时的位置，理由还是 [键盘驱动] 讲过的那个。SYN_DROPPED 到来的时候，按钮的队列就清空，翻译器的状态跟着清零，注释的口径是重建胜过相信。这次重建跟内核侧的恢复是同一个词的两头，内核补发记录、用户态删掉自己的状态。

翻译的活分两层做，我们把两层的分工分开讲。里层是 visor 的 evdev 翻译器（`bindings/evdev/event_wire.hpp`），它是一个只管翻译的纯函数，一个词换一个可能没有的事件。几个立场的出处注释里都有：WireEvent 只镜像 type、code、value 三个读得到的字段，时间戳是故意缺席的，因为 64 位与 32 位 Linux 的 timeval 宽度不一样，所以翻译器干脆不读它。注释说没有事件之类的回答是正常的，返回类型选的是 optional，它不长一条没有决策依据的错误通道。箭头键的 evdev 码 103 到 108 落在可打印 ASCII 的区间里，翻译器把它们映射到核心的带外值。修饰键的词进来的时刻把自己的状态改好，跟在它后面的事件就带着新的标志位。

外层是 vesper 的 EvdevTranslator，干两件 visor 留给宿主的折叠，我们把它们分开看。头一件是绝对轴的折叠：数位板的采样报的是整块物理屏上的位置，缩放进屏幕之后再跟当前的指针求差，把差值当相对位移喂给 visor 的相对翻译器。头文件注释把这里的讲究写全了：第一次采样是落位不是基线，当成基线的话每次点击都会带偏移。求差用的是实际限位之后的指针位置，边缘的回折与绝对相对混着的流都不会攒出漂移。另一件是文本的折叠，键位码要走一遍美式的键位表，可打印字符骑在自己的码点上，终端读的就是这批文本。

事件进树的入口也归我们认一认，它在 visor 的 HostTable（`bindings/host.hpp`）：poll_event 是宿主填的函数指针，答的问题始终是现在有没有事件，而且从不阻塞。`fbdev_desktop.cpp` 的 `fbdev_poll_event` 把两台设备接上，两边都有话说的时候键盘占这一轮，这句的出处注释 [键盘驱动] 已经记过了。主循环的睡眠是一次 poll(2)：三个座席（键盘、指针、shell 的主端）挂着 POLLIN，超时设的是 15 毫秒，有货的座席叫醒桌面，安静的桌面把节拍睡掉。退场的 shell 把主端永远挂在断开的线上，它的槽位就从 poll 里撤走（负的 fd 会被忽略），注释说不然的话每个节拍都会在同一根挂断上醒过来。

拿到事件之后的处理，我们看的是 run_dirty 的一轮（`visor/ui/run_loop.hpp`），一轮干的是三件事：新到的事件交给控件树消化，欠着的条带重画出来，欠下的重画清掉。注释给它写的节奏规则是这一页要留的重头：一轮至多消费一个键盘事件，剩下的排给后面的轮次，所以一次按下与它的抬起永远不会同轮，控件在两次之间显示的按下状态至少拥有一次完整的重画，这个时序是结构性的，既不借宿主的重复行为也不借时钟。指针的事件随到随清，注释说指针的事件是位置的采样，节拍那一套安不到指针的头上。跟旧世界的那套集成比，刷屏的驱动源换了：旧的集成是 PIT 节拍定时整屏合成（旧文档记的那个世界），现在的做法是谁欠了重画谁去画，到点刷屏的事再也没有了。

## 源码位置

- git 提交 `cde30d1`（2026-05-25，`migrate from old Cinux`） — `kernel/gui` 随旧仓库迁入
- git 提交 `dd0e18f`（2026-07-05） — 内核桌面层（window_manager、window、terminal 等）删除，桌面切用户态库
- git 提交 `45378a1`（2026-07-06） — /dev/event0 输入到用户态（双写监听器）
- git 提交 `62ae31d`（2026-07-06） — fork+execve 拉起 `user/cinux_gui_host`
- git 提交 `49ddfb0`（2026-10-31） — `kernel/gui` 剩余部分、`libs/gui` 整库、`kernel/drivers/canvas` 与 CINUX_GUI 开关删除，旧文用 `git show 49ddfb0^:路径` 回看
- `kernel/driver/input/event_stream.hpp/cpp` — EventStream 本体：publish、溢出恢复、独占、问句
- `kernel/driver/input/input_config.hpp` — 24 字节记录、64 条队列、问句字与主从号
- `kernel/fs/devfs/input_node.cpp` — event0 与 event1 的座位、InputOpen 的引用计数
- `kernel/driver/keyboard/keyboard.cpp` — grab_hook 的接线处
- `third_party/vesper/src/host/evdev_input.hpp/cpp` — EvdevInput 与 EvdevTranslator：打开、独占、排空、两件折叠
- `third_party/vesper/third_party/visor/include/visor/bindings/evdev/event_wire.hpp` — WireEvent、EvdevState 与 visor 的翻译器
- `third_party/vesper/third_party/visor/include/visor/bindings/host.hpp` — HostTable 与 poll_event
- `third_party/vesper/third_party/visor/include/visor/ui/run_loop.hpp` — run_dirty 与一轮一个键盘事件的节奏规则
- `third_party/vesper/examples/fbdev_desktop.cpp` — fbdev_poll_event、三个座席的 poll 主循环
- `third_party/vesper/src/ui/desktop/desktop.hpp` — Desktop、WindowFrame、Terminal 桌面控件
