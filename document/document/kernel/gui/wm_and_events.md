# 窗口管理器与事件系统

## 功能概述

这一页讲的是事件链路是怎么走的，咱们跟着事件从内核那一头走到桌面那一头。窗口、Z-order、事件循环这些桌面的活都在用户态，桌面住的是 vesper（`third_party/vesper` 这个 submodule）里 `vesper::desktop` 的控件树。内核在窗口管理这一半留下的只有输入事件流：驱动把事件送进各自的流，devfs 摆出来的文件名是 /dev/event0 与 /dev/event1，桌面从那里领走自己的事件。

分工咱们在这里画清。驱动怎么生产事件（扫描码、三字节包、座位怎么落）的事，归 [键盘驱动](../drivers/keyboard.md) 与 [指点设备](../drivers/mouse.md) 两页讲过了，绘图与 /dev/fb0 的座位归 [Canvas 画布](../drivers/canvas.md)，本页管的是事件离开设备节点之后的路：内核的 EventStream（事件流，一台输入设备自己的事件队列）怎么接住，独占的问话怎么答，桌面那头的 vesper 怎么消费，visor（vesper 自带的 UI 库，源码住在 `third_party/vesper/third_party/visor` 这个子模块里）的主循环节奏是什么，它们都是本页要讲的内容。

## 内核侧：EventStream 怎么接住事件

EventStream 的家在 `kernel/driver/input/event_stream.hpp/cpp`，一台设备配一条自己的流。文件头注释给它的定位是事件花名册：一条条记录从 IRQ 递进来，读者领走的始终是整条的记录，独占的那个字决定还有谁能听见。生产的一侧是 publish，注释对它的要求是在中断上下文里也安全，publish 干的活是往队里放一条记录，做的全是整数运算，里面没有一条是拖节奏的动作。每条记录进门的时候打上自己的时间戳，秒与微秒从 Tick 的节拍换算出来，形状咱们对着 Linux 的 `struct input_event` 核过一遍，两边的 24 个字节一模一样。队列是 64 条的环形，`input_config.hpp` 的注释替队列表明了立场，丢掉的爆发只算一个错过的手势，到货的记录一句也不虚报。

溢出的恢复，[键盘驱动](../drivers/keyboard.md) 那一页已经照着注释把次序走过一遍了。这一页补的是它没有写出名字的两个成员，publish 这头的 `dropped_` 标志与 read 这头的 `delivered_keys_` 花名册，咱们把它们挨个接进次序里认。队列满了的时候 publish 把整条队列清空，把 `dropped_` 立成丢了东西的标志，然后把当前的记录放进空出来的队列。下一次 read 领到的头一条记录是 SYN_DROPPED（EV_SYN 里 code 3 的那个码），它把丢过东西的消息报给读者。接着流按 `delivered_keys_` 的花名册把已经送出去的按键逐个补发释放，绝对轴的设备再把两根轴的当前位置重放一遍，末尾的一条 SYN_REPORT 把报告合上。读的人拿到的是一份自洽的状态，按住的键不会悬在半空。用户态那头见到 SYN_DROPPED 的反应是重置整份翻译状态，这个反应的现场归后面 vesper 的段落讲。同一段路的两头各有各的做法，内核补发记录、用户态清掉自己的翻译状态。

ioctl 的问句，流认得的有三类，咱们拿 answer 把它们一个个地对过去。EVIOCGKEY 答当前的按键位图，流跟踪的事件码有 0x300 个，一码一位、八码一字节地摊开。EVIOCGABS 答绝对轴的量程与当前位置。EVIOCGRAB 管的则是独占，规则的次序源码注释排得很清：一次独立的 open 持有一份独占，已有的独占连自己再要一次也答 EBUSY，别人来放的话答案是 EINVAL，最后一个描述符关闭的时候 `release_owner` 把独占放掉、顺手唤起 grab_hook。grab_hook 是接线注册的函数指针，键盘驱动在 `attach()` 里装的是 `Keyboard::grab_hook`，独占的消息一来，它就把键盘的字形开关翻成假，控制台那一路就收不到键了，这段现场归 [键盘驱动](../drivers/keyboard.md) 的读者小节讲。流还留着 `poll_hook` 的座位，报告要等抽干环形队列的设备（数位板那一路）把自己的 drain 交过来，read 与 poll 进门的时候都会跑它，不来中断的读者也拿得到报告。

devfs 的这一层很薄，咱们把座位的落点也看了：座位落在 `kernel/fs/devfs/input_node.cpp`，event0 坐的是从号 64，event1 坐的是从号 65，主号 13 是 Linux 的 evdev 用的那个数（`kInputEventMajor` 的注释原话）。每份 open 对应一个引用计数管理的 InputOpen，析构的时候 `release_owner` 放独占，read、has_input、ioctl 三个动词做的事都是原样转发给流，write 的回答是不支持。座位的落法、event1 在数位板与 PS/2 鼠标之间的挑法，[键盘驱动](../drivers/keyboard.md) 与 [指点设备](../drivers/mouse.md) 两页都写过了，这里就不再重复了。

## 用户态：vesper 怎么消费

桌面那头的一台设备对应一个 EvdevInput（`third_party/vesper/src/host/evdev_input.hpp/cpp`）。open 拿 `O_RDONLY | O_NONBLOCK` 这样的旗子开节点，紧跟着发出去的问话是 EVIOCGRAB，独占的姿态是尽力而为，已被别人抓着的设备照读不误，绝对轴的设备再用 EVIOCGABS 把两根轴的真实量程读出来。量程折算的细节 [键盘驱动](../drivers/keyboard.md) 已经讲过了，翻译器的分配那一页也写过。咱们在这里把剩下的活接进 open 与独占的次序里完整走一遍，头一件是开机的种子：内核不必在 open 之后重复没变的轴，vesper 把读到的两份当前位置各喂一次翻译器，注释的说法是静止的一次点击也要带着设备的坐标。翻译器的分配跟着排进这个次序，每个设备各配各的，咱们在头文件注释里找得到这么分的理由，[键盘驱动](../drivers/keyboard.md) 那一页替咱们记下了。

poll_next 把排空的节奏定成了非阻塞：一条一条 `input_event` 地领，读不满一整条的时候它就带着半截的状态返回，剩下的下一轮接着处理。指针按钮的记录攒在按钮的队列里，等的是 SYN_REPORT 再结算，命中测试要用报告收尾时的位置，其中的道理咱们在 [键盘驱动](../drivers/keyboard.md) 里见过。SYN_DROPPED 到来的时候，按钮的队列就清空，翻译器的状态跟着清零，注释的口径是重建胜过相信。

翻译的活分两层做。里层是 visor 的 evdev 翻译器（`third_party/vesper/third_party/visor/include/visor/bindings/evdev/event_wire.hpp`），它是一个只管翻译的纯函数，一条记录换一个可能没有的事件。几个立场的出处注释里都有，咱们对着原文把它们认了一遍：WireEvent 只镜像 type、code、value 三个读得到的字段，时间戳是故意缺席的，因为 64 位与 32 位 Linux 的 timeval 宽度不一样，所以翻译器干脆不读它。注释说没有事件是正常的回答，所以返回类型选的是 optional，翻不出结果的时候，空值本身就是咱们要的答案。注释给这个选择也留了理由：没有决策可依的错误通道开了也没有用处，它就把这样的通道省了。箭头键的 evdev 码 103 到 108 落在可打印 ASCII 的区间里，翻译器把它们映射到核心的带外值。修饰键的记录进来的时刻把自己的状态改好，跟在它后面的事件就带着新的标志位。

外层是 vesper 的 EvdevTranslator，干两件 visor 留给宿主的折叠，咱们把它们分开看。头一件是绝对轴的折叠：数位板的采样报的是整块物理屏上的位置，缩放进屏幕之后再跟当前的指针求差，把差值当相对位移喂给 visor 的相对翻译器。头文件注释把这里的讲究写全了：第一次采样是落位不是基线，当成基线的话每次点击都会带偏移。求差用的是实际限位之后的指针位置，边缘的回折与绝对相对混着的流都不会攒出漂移。另一件是文本的折叠，键位码要走一遍美式的键位表，可打印字符骑在自己的码点上，终端读的就是这批文本。

事件进树走的入口是 visor 的 HostTable（`third_party/vesper/third_party/visor/include/visor/bindings/host.hpp`）：poll_event 是宿主填的函数指针，答的问题始终是现在有没有事件，而且从不阻塞。`fbdev_desktop.cpp` 的 `fbdev_poll_event` 把两台设备接上，咱们照着这段接线把座次也认了一遍：两边都有话说的时候键盘占这一轮，这句的出处注释 [键盘驱动](../drivers/keyboard.md) 已经记过了。主循环的睡眠是一次 poll(2)：三个座席（键盘、指针、shell 的主端）挂着 POLLIN，超时设的是 15 毫秒，有货的座席叫醒桌面，安静的桌面把节拍睡掉。已经退出的 shell 把主端永远挂在断开的线上，它的槽位就从 poll 里撤走（负的 fd 会被忽略），注释说不然的话每个节拍都会在同一根挂断上醒过来。

拿到事件之后的处理，咱们看的是 run_dirty 的一轮（`third_party/vesper/third_party/visor/include/visor/ui/run_loop.hpp`），一轮干的是三件事：新到的事件交给控件树消化，欠着的条带重画出来，欠下的重画清掉。注释给它写的节奏规则是这一页要留的重头：一轮至多消费一个键盘事件，剩下的排给后面的轮次，所以一次按下与它的抬起永远不会同轮，控件在两次之间显示的按下状态至少拥有一次完整的重画，这个时序是结构性的，既不借宿主的重复行为也不借时钟。指针的事件随到随清，注释说指针的事件是位置的采样，节拍那一套安不到指针的头上。

## 源码位置

- `kernel/driver/input/event_stream.hpp/cpp` — EventStream 本体：publish、溢出恢复、独占、问句
- `kernel/driver/input/input_config.hpp` — 24 字节记录、64 条队列、问句字与主从号
- `kernel/fs/devfs/input_node.cpp` — event0 与 event1 的座位、InputOpen 的引用计数
- `kernel/driver/keyboard/keyboard.cpp` — grab_hook 的接线处
- `third_party/vesper/src/host/evdev_input.hpp/cpp` — EvdevInput 与 EvdevTranslator：打开、独占、排空、两件折叠
- `third_party/vesper/third_party/visor/include/visor/bindings/evdev/event_wire.hpp` — WireEvent、EvdevState 与 visor 的翻译器
- `third_party/vesper/third_party/visor/include/visor/bindings/host.hpp` — HostTable 与 poll_event
- `third_party/vesper/third_party/visor/include/visor/ui/run_loop.hpp` — run_dirty 与一轮一个键盘事件的节奏规则
- `third_party/vesper/examples/fbdev_desktop.cpp` — fbdev_poll_event、三个座席的 poll 主循环
- `third_party/vesper/src/ui/desktop/desktop.hpp` — Desktop 桌面控件树的根
- `third_party/vesper/src/ui/window_frame/window_frame.hpp` — WindowFrame 窗框控件
- `third_party/vesper/apps/terminal/ui/terminal/terminal.hpp` — Terminal 终端控件
