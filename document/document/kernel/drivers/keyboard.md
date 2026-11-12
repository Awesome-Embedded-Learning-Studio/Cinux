# 键盘驱动

## 功能概述

这一页咱们把 `kernel/driver/keyboard` 里 PS/2 键盘的故事从头走一遍。键盘的每个键落下、抬起，8042 控制器就在 IRQ1 的线上举一次手，驱动从 0x60 的数据口把字节读走并翻成两种面貌。字符面貌进自己的环形队列，等的是控制台来取。键位面貌走的是 EventStream（事件流，一台输入设备自己的事件队列），被 devfs（内核里把设备摆成文件的那一层）摆成 event0 的输入节点，桌面 vesper 读的就是它。扫描码的翻译住在 `scancode.hpp` 与 `scancode.cpp`，设备本体的家在 `keyboard.hpp` 与 `keyboard.cpp`，事件流的记录格式与独占（grab）的问答在 `kernel/driver/input`，座位落在了 `kernel/fs/devfs/input_node.cpp`。

这一页的读法跟着一次按键走，咱们看字节从数据口进来之后怎么翻、怎么进两条队列，再看桌面把它拿走的时候内核交出什么、谁有权让控制台闭嘴。指点设备里跟键盘共用 8042 的只有 PS/2 鼠标，USB 数位板走的是 XHCI（USB 的主控器）的另一条路，两台设备在事件面上各坐各的座位，分工的界线咱们在最后一节画清。桌面的绘图与提交在 [Canvas 画布](canvas.md)那一页讲过了，这一页补的是输入的那一半。

## 8042 控制器与 IRQ1

键盘与辅助指针共用的是同一个 8042 控制器，端口的地址、命令的字与应答的纪律因此都收在 `kernel/driver/ps2/controller.hpp` 一处，头文件的注释把这层用意写明了。控制器还有一条对外的接缝 `Ps2Ports`，内核把它接到真实的端口读写上，host 测试接的是照剧本应答的替身，两条接线咱们都认了一遍。启动序列里每一步等待用的都是独立于中断的延时，注释说设备缺席的时候，启动也要在有限的时间里走完，bring-up 不会挂在一张沉默的芯片上。

启动的头两步用 `0xAD` 与 `0xA7` 把两个端口都关掉，`Drain` 冲掉输出缓冲里积着的旧字节，咱们用 `0x20` 读回配置字节，把第一端口的 IRQ 门（0x01）与翻译位（0x40）竖起来、把第二端口的 IRQ 门（0x02）按下去，写回配置走的是 `0x60`，`0xAA` 的自检命令发出去，答上来的字是 `0x55` 才算健康，末了 `0xAE` 把第一端口重新打开。翻译位竖起来有它的用处：键盘报键用的编号叫扫描码，编号的惯例分成好几套，控制器把设备交上的扫描码折成第 1 套再给软件，驱动从头到尾面对的就只有第 1 套。

`attach()` 被引导序列安排在了 `init()` 之后，咱们看见它做三件事，一件是把 grab 的钩子装到事件流上，一件是给 IRQ1 的线坐上 handler，一件是把线打开了。它的注释提醒接线的人，中断服务必须已经握着自己的芯片，这样的次序在引导序列里排在了键盘这一步的前面，apic interrupt delivery 那一步把 IO APIC 装进了中断服务，keyboard 这一步才轮到设备的 `init()` 与 `attach()`。IRQ1 走的向量是 0x21，算的就是线号加上 0x20，IO APIC 的路由就按这个数填。线上来的时候，模板烙出来的中断出口把帧交给 `Irq::dispatch`，dispatch 跑完了 handler 顺手应答芯片，`irq.hpp` 的注释写明应答完全是中断服务内部的活，EOI 从来不出自任何驱动与桩的手。出口在返回之前还要过一遍调度器的抢占检查，那段故事在 [进程与调度器](../proc/process_and_scheduler.md)里讲过了。

IRQ1 的 handler 短得只有一行，咱们把它原样搬来：

```cpp
// kernel/driver/keyboard/keyboard.cpp
void irq1_thunk() {
    cinux::driver::Keyboard::self().on_byte(cinux::driver::InB(kDataPort));
}
```

咱们把注补在这里：引文与源文件的这一段逐字相同，常量 `kDataPort` 在文件顶部用 using 接进了匿名命名空间，展开就是 0x60 的数据口。

引导序列对两台设备的态度不一样，咱们把差别记在这里。键盘的 `init()` 答了假，这一步打出的就是 `[kern] init 'keyboard' failed, halting` 的停机行，机器停在了原地，因为打字的设备装不上，后面的每一步都白搭。指点设备没有这样的待遇，鼠标初始化的结果被原样丢掉，鼠标缺席是允许的事，开机的路照常走。

## 一次字节的两副面孔

`on_byte` 是两副面孔分岔的地方，咱们把它的全身读一遍：

```cpp
// kernel/driver/keyboard/keyboard.cpp
void Keyboard::on_byte(uint8_t raw_byte) {
    char const kGlyph = TranslateScancode(state_, raw_byte);
    if (kGlyph != 0 && glyphs_live_) {
        events_.push(kGlyph);
        readable_.post();
    }
    KeycodeEvent word{};
    if (TranslateScancodeKeycode(codes_, raw_byte, word)) {
        stream_.publish(kEvKey, word.code, word.pressed ? 1 : 0);
        stream_.publish_report();
    }
}
```

进来的是同一个字节，两个翻译器却各走各的路。成员的名字容易认错，装字符的是 `events_`，扛事件流的是 `stream_`，咱们在这页里把两个名字分死。字形面回答的是这个键打出什么字符。第 1 套扫描码的妙处在抬起码就是按下码加上一个 0x80 位，按下与抬起在掩码的作用下分作两半，scancode.hpp 的开篇把选它的理由写在那里。两张 64 格的表按槽位给字符，make 码（按下一键时控制器送来的那个编号）就是槽位号，平时的字符归 `kScancodeLower` 管，Shift 按住的时刻归 `kScancodeUpper`，零格与 64 以外的 make 码在表里对不上字符。Shift 的左右两只键折成一个开关，注释说两只手的键谁按着都算数。Ctrl 的左右两只分开记，右 Ctrl 走的是扩展的路，`0xE0` 的引导字节（一组两字节里的头一个）走在前面，右 Ctrl 的字节跟在它的后面。Ctrl 按住的时候字母交出的就是控制字符，`a` 给出的就是 0x01，`@` 到 `_` 的那一段按减 `@` 算。扩展组（`0xE0` 领起的那一群，方向键与右手的键都在里面）在字形面默认整组丢弃，注释的态度是方向键在这里不出事件。例外的只有右 Ctrl 一处，它的按下抬起会被单独记下，其余的扩展键一概交不出字符。

翻出来的字符进 64 槽的环形队列，这点家底咱们照着容量常量 `kKeyboardEventCapacity` 的注释认了一遍：队列要留出一个空槽，满与空的界线才分得开，真正装字符的是 63 槽。push 遇上满队列的时候答的是假，环形队列自己的注释把这层立场写着：丢不丢事件的决定在调用者手里，队列满的时候，新来的字符会被丢掉，队里的一个不动。取字符的口是 `take(bool wait)`，等待的那一路睡在信号量 `readable_` 上，进来的每一个字符都放出一个数，取的人在队空的时候把数花掉再查一眼，函数的注释把设计收成一句：IRQ1 的唤醒就是全部的设计。等待与唤醒的底细在 [同步原语](../proc/sync.md)那一页讲过，控制台今天用的是不等待的那一路，队空的时候就答零。

键位面回答的是哪个键、往哪个方向动，咱们把它的路数认一遍。主块的 make 码本身就是键位码，注释说除扩展组以外的都不用表，表里 17 个成员认的是 Linux evdev 的编号：小键盘回车 96、右 Ctrl 97、右 Alt 100，光标区从 Home 的 102 排到 Delete 的 111，两枚 Meta 键（键盘上带徽标的那两枚键，Windows 键盘上就是两面窗标那一对）拿的是 125 与 126，用户态的键位表读的正是同一批数。左 Shift 的码是 42，左 Ctrl 的码是 29，左 Alt 的码是 56，它们走的都是主块直通。Pause 的多字节引导 `0xE1` 与另一家厂商的多字节引导 `0xE2` 被整字节忽略，注释给它们各自留了名分，厂商的名字注释没有给出。每个报出来的键都跟着一个同步字收尾，同步字就是 `EV_SYN` 类的那个字，报告收尾用的成员叫 `SYN_REPORT`，`publish_report` 干的就是这件事，input_config.hpp 的注释说一份按键或指针的报告以同步字落幕，与 Linux 自家设备的节拍一致，攒到报告收尾才结算的消费者不用改一个字。

两个翻译器各养各的 `0xE0` 状态，这里有一处咱们想指给你看的安排。scancode.hpp 的注释把理由交代了：字形翻译器从共享的状态里吃掉引导字节，所以键位面自带一个。字节流来的是同一条，两个消费者各自认各自的引导字节，哪天一边改了扩展组的处理，另一边的处理不会被拖下水。

## 事件面：24 字节的记录

EventStream 是每台输入设备自己的事件队列，中断往里推的是新事件，读者从里往外拿的是旧记录。记录的形状是 24 字节的 `InputEventWire`，咱们把记录分开数一遍：两个 8 字节的时间字，跟着的 type 与 code 各占 2 字节，殿后的 value 独占 4 字节，`static_assert` 把 24 这个数守住了。input_config.hpp 的注释把这层安排的动机写在开篇：记录逐字节照着 Linux 的 `struct input_event` 摆，因为提问的一方是拿着 libc 的 `linux/input.h` 编出来的用户程序，形状本身就是两边共同守着的声明。时间戳取的是节拍服务的钟，节拍的频率是 100 Hz，换算常量的名字就叫戳的分辨率，一次节拍之内落下的两个键，拿到的戳是一样的。

队列的容量是 64 条记录，注释给丢弃记号的立场是，丢掉的记号不撒谎，它如实说中间漏了一截。溢出的时候，排队的旧记录整圈让出去，最新的一条记录留了下来，丢弃的记号跟着竖起来，`SYN_DROPPED` 就是这么来的。publish 的注释把后面的剧情也排好了，咱们照着它读：read 的头一句是 `EV_SYN` 的 `SYN_DROPPED`，跟着把已经交给读者的键逐个补发抬起，配了绝对轴的流再把轴的最新位置补上，末了再由一个 `SYN_REPORT` 把恢复收尾。重建的每一步都发生在 read 的循环里，而不在中断里。

read 的纪律是整条记录进出，咱们把它读清。请求的字节数不足一条记录，这样的请求当场拒绝，注释的立场是半条记录不过线，只有整倍数的记录才数。有人独占了流的时候，别人的 read 拿到的是 kWouldBlock，也就是用户态的 EAGAIN，poll 的回答只是一个假，非阻塞的桌面靠 read 的这个答案就知道设备此刻不归自己。

ioctl（用户态拿问句敲设备描述符的口子）的三个问句咱们分开看一遍。`EVIOCGKEY(len)` 答的是一张当前按着的键位图，图盖住的是全部 768 个键位，一个键占的是一位，拷给调用者的字节数在问句里报的长度与 96 字节的上限之间取小。`EVIOCGABS` 答绝对轴的现值与量程，只给配了绝对轴的流。`EVIOCGRAB` 回答的是设备归谁：一次独立的打开拥有一份独占，已有独占的时候连主人自己再抢一次都答 EBUSY，别人的释放答 EINVAL，最后一个描述符关掉的时候独占自动松手，answer 的注释把这几条边界排得清清楚楚。独占落下的那一刻钩子被唤起，键盘这一侧的故事接到了后面的 devfs 与读者两节。

## devfs 的座位：event0 与 event1

座位是 `kernel/fs/devfs/input_node.cpp` 摆下的，咱们把落座的参数读一遍。落座的口是 `RegisterInputDevices`，它把两条流坐进了设备模型，名字取的是 event0 与 event1，坐的都是字符设备，主号 13（注释说这正是 Linux evdev 用的那个数），从号定的是 event0 的 64 与 event1 的 65，总线写的是 virtual，驱动名写的是 input。inode 的号从 0x2000 起按从号发，stat 拿主从号把字符设备的身份拼回来，`kInputInoBase` 的注释把这套身世写了。

打开节点的时候，内核就造一个带着引用计数的 InputOpen，它握着流与自己的操作表，咱们挨项认过去：read 的活交给流，ioctl 的问话也交给流，write 的回答永远是 kNotSupported，输入节点里没有写字的事。描述符关闭的时候 InputOpen 析构，顺手把归它的独占松掉，这正是上一节说的最后关闭自动放手。头文件的注释还给空座位留了一句：没有设备在后面的座位答不了任何输入，是一个诚实的空节点。

接线在 `kernel/fs/file_world.cpp` 的 `FileWorld::init` 里，咱们把用到的三行搬在下面：

```cpp
// kernel/fs/file_world.cpp
const bool kTablet = driver::UsbTablet::self().init();
RegisterInputDevices(
    driver::Keyboard::self().events(),
    kTablet ? driver::UsbTablet::self().events() : driver::Ps2Mouse::self().events());
```

这段接线的意思咱们翻成白话：event0 坐的永远是 PS/2 键盘的事件流，替补的事不存在。event1 的归属看数位板的脸色：数位板枚举成功了就归它坐，枚举失败了，坐上去的就是 PS/2 鼠标。USB 键盘的身份是第三种，事件流里没有它的名字，它自己坐的是 usbkbd（主 10 从 0）的座位，它的字形只走控制台那一路。

## 读者：控制台与桌面

控制台那头的取键回调一轮扫三个来源，排在前头的是串口，注释说宿主环境里跑的时候人是在串口上打字的，然后才是 QEMU 监视器喂的 PS/2 键盘，收尾的是 USB 键盘。回显落在了文字控制台上，键怎么进终端的行编辑是 tty 的事，咱们这一页只认到取键为止。阻塞读的实现住在 `kernel/syscall/sys_read.cpp`，设备答 kWouldBlock 的时候调用任务让出 CPU 再试，非阻塞打开的描述符拿到的是 EAGAIN，桌面用的正是非阻塞这一路。

桌面 vesper 那头的入口是 `EvdevInput::open`，咱们跟着它进门：打开用的就是 `O_NONBLOCK`，紧跟着发出的一声问话是 `EVIOCGRAB`，注释说这一步尽力而为，就算独占落在了别人手里，打开的人也照读不误。独占在内核那头唤起的钩子把键盘的 `glyphs_live_` 翻成假，字形从此不再进控制台的队列，input_node.hpp 的简介把这句话放在了开头：独占了键盘，正是让控制台安静下来的那个安排。打字的人感觉不到底下还有控制台在听。

读的一路每次要一整条记录，一条的大小是 24 字节，读进来的记录交给 `EvdevTranslator`。键位码过美式键位表折成了字符，注释的说法是可打印字符骑在码点上，终端读的就是这批文本。每个设备都有自己的翻译器，键盘的状态装修饰键，指针的状态装位置，注释的理由是两台设备合成一个状态的话，数位板的记录会把按住的 Shift 冲掉。指针的按钮等到 `SYN_REPORT` 才结算，注释的理由是 Linux 可能把按钮排在坐标前面，命中测试要用报告收尾时的位置。`SYN_DROPPED` 一到就重置翻译器的整份状态，注释的口径是重建胜过相信。绝对轴的折算咱们也要对一遍，QEMU 的 usb-tablet 报的是 0..32767 的量程，vesper 拿 `EVIOCGABS` 读真实的量程折到屏幕上，读不出量程的设备按一比一走。

桌面的主循环里键盘排在指针前面，这样的偏心咱们照着 fbdev_desktop.cpp 的注释记下：两边都有话说的时候，这一轮的席位归打字，指针的记录在它的节点里等下一轮。gui 会话（会话名等于 gui 的那一路，[Canvas 画布](canvas.md)交代过）里 `/etc/init.d/desktop` 执行的命令是 `/bin/vesper /dev/fb0 /dev/event0 /dev/event1`，devfs 座位的名字直接当参数递给了程序。例子代码自带的默认值是 Linux 形状的 `/dev/input/event0`，在 CinuxOS 上被传进来的参数盖掉。

## 与指点设备的分工

键盘与 PS/2 鼠标共享的仍是同一个 8042，线的分配却是一人一根，咱们把两根线的归属认一遍：键盘举的是 IRQ1，鼠标举的是 IRQ12，走的向量分别是 0x21 与 0x2C。键盘启动的时候把配置字节里第二端口的门按了下去，鼠标启动的末尾再把它竖起来，两个驱动各管各的时刻。鼠标的 bring-up 有一层自己的纪律，头文件的注释排了次序：与设备一问一答地把端口打开、默认值设好、报告放行，这些走完了，IRQ12 的线才开，设备答上来的应答字节就不会落进任何 handler 的手里被误读。它还要求键盘的自检已经走完，两个驱动的出场次序就这样在引导序列里定了下来。

数据的形状两边也不同，咱们把两边的形状对一遍。鼠标说的是三字节一份的报告，解码器在找包头的时候把不像头的字节直接丢掉，注释说报文里常置的标志位与清着的溢出位就是头的签名，迷路的流在下一个头自己找回队形。位移报的是相对量，只有非零的位移才有自己的记录，按键报的是边沿，0x110 到 0x112 的三只键，每份报告的末尾都跟着同步字。按键的两边都是 `EV_KEY`，刚才的三只键在两边同名，鼠标的位移走 `EV_REL`，数位板的坐标走 `EV_ABS`，键盘的一侧没有位移也没有坐标，它报的只有 `EV_KEY`。数位板把两根轴的量程配成 0..32767，又把抽干 XHCI 事件的钩子交给流，`set_poll_hook` 的注释说这样不来中断的读者也能拿到报告。缺席的宽容程度也排了序：键盘缺席的时候机器停摆，鼠标缺席了也没人追问，数位板缺席就由鼠标顶上 event1 的座位。

## 源码位置

- `kernel/driver/keyboard/keyboard.hpp`、`keyboard.cpp`、`keyboard_config.hpp` — 设备本体、8042 启动、IRQ1 馈送、字形队列与信号量
- `kernel/driver/keyboard/scancode.hpp`、`scancode.cpp` — 第 1 套扫描码的两张字形表、修饰键状态与两副翻译面孔
- `kernel/driver/ps2/controller.hpp`、`controller.cpp`、`controller_host.cpp` — 8042 的事务词汇与端口接缝
- `kernel/driver/input/event_stream.hpp`、`event_stream.cpp`、`input_config.hpp` — 事件流、24 字节记录、SYN_DROPPED 恢复、grab 与 ioctl 词汇
- `kernel/driver/mouse/ps2_mouse.hpp`、`ps2_mouse.cpp`、`mouse_packet.hpp` — IRQ12 的指针、三字节包与再同步
- `kernel/driver/usb/usb_tablet.hpp`、`usb_tablet.cpp` — 绝对轴数位板与抽干钩子
- `kernel/driver/usb/usb_keyboard.hpp`、`usb_keyboard.cpp` — 只进控制台的 USB 键盘与 usbkbd 座位
- `kernel/interrupt/irq.hpp`、`kernel/arch/x86_64/irq_stubs.cpp`、`kernel/arch/x86_64/vector_config.hpp` — 中断服务、0x20 起的向量与内部应答
- `kernel/init/init_sequence.cpp` — keyboard 与 pointer 两个引导步骤及其失败态度
- `kernel/fs/devfs/input_node.hpp`、`input_node.cpp` — event0 与 event1 的落座、每次打开的 InputOpen
- `kernel/fs/file_world.cpp` — 两条流的接线与 event1 的归属
- `kernel/driver/tty/console_tty.cpp` — 控制台的三路取键与回显
- `kernel/syscall/sys_read.cpp` — 设备读的让出循环与非阻塞短路
- `third_party/vesper/src/host/evdev_input.hpp`、`evdev_input.cpp` — 打开、独占、翻译与 SYN_DROPPED 重建
- `third_party/vesper/examples/fbdev_desktop.cpp` — 桌面的两路输入与键盘靠前
- `test/unit/test_input.cpp`、`test/unit/test_input_node.cpp`、`test/kernel/test_keyboard.cpp` — 事件流、座位与注入字节的测试
- `user/rootfs/buildroot/overlay/etc/init.d/desktop` — gui 会话的拉起命令
