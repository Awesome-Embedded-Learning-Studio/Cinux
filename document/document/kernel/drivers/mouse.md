# 指点设备

## 功能概述

这一页我们把 `kernel/driver` 里两台指点设备的路各走一遍。PS/2 鼠标坐的是 8042 控制器（键盘与它共用的那颗芯片，[键盘驱动](keyboard.md)讲过底细）的第二端口，报的是三字节一份的数据包，IRQ12 的线上举一次手，就把相对位移与按键的边沿投进事件流（一台输入设备自己的事件队列）。USB 数位板走的是 XHCI（USB 的主控器）的另一条路，报的坐标是绝对位置，定下的量程是 0..32767。两条路在 `kernel/fs/file_world.cpp` 的接线上汇成同一个座位，event1 的归属要看数位板的脸色，枚举成功了归它坐，枚举失败了坐上去的就是 PS/2 鼠标。

这一页的读法跟着报告走，我们看字节怎么翻成事件，看设备的开机纪律为什么这么排，再看两条路怎么汇进同一个座位。桌面的绘图与提交在 [Canvas 画布](canvas.md)那一页讲过了，这一页补的是指针的输入侧，与键盘的分工我们放在最后一节画清。

## PS/2 鼠标的三字节包

设备本体的家在 `ps2_mouse.hpp` 与 `ps2_mouse.cpp`，语法（包字节怎么读）的家在 `mouse_packet.hpp` 与 `mouse_packet.cpp`。头文件的注释把这层分开的用意写明了：解码器是纯粹的语法，端口是碰不到的，host 测试拿普通的字节就能驱动它，单例扛的才是设备的生命周期。我们从解码器读起，因为它回答的是最基本的问题：三个字节怎么变成一份位移加按键的报告。

一份报告占的是三个字节，布局是固定的。头一个字节装的全是零碎的位，低三位装的是按键（左 0x01、右 0x02、中 0x04），第四位是恒置着 1 的，再往上的两位装的是两根轴各自的符号，顶上清着的两位溢出标记也在头一个字节里，这样的恒置位与溢出标记合起来构成了包头的签名。第二与第三字节装的是 X 与 Y 的位移数值。feed 的找头纪律我们原样搬一段下来：

```cpp
// kernel/driver/mouse/mouse_packet.cpp
if (seated_ == 0) {
    if (!kHeadByte.has(base::bit::BitMask<uint8_t>{kHeadAlwaysOne}) ||
        kHeadByte.extract(kHeadOverflowSegment) != 0) {
        return false;
    }
    bytes_[0] = raw_byte;
    seated_   = 1;
    return false;
}
```

坐在找头位置上的字节，要看的是两个条件，恒置位竖着、顶上的溢出位清着，缺了任何一个，feed 就把这个字节丢掉了，不进任何的缓冲。`mouse_packet.hpp` 的注释把这层立场写着：不能当包头的字节不值得缓冲，丢掉才是比缓冲诚实的选择，迷路的流在下一个包头自己找回队形。三个字节坐齐了之后，解码就是一步的事了，数值的字节本身是 8 位宽，符号却是竖在包头里的，X 的符号位（0x10）竖着的时候数值就要减去 0x100，9 位的补码就算完了，Y 的符号位（0x20）也是同样的算式。还有一处安排我们要单独指给你看，解码的出口把 Y 翻了一次，写的是 `out.dy = -step_y`，因为 MouseSample（解码出来的样本）的注释把 dy 的正向定在屏幕的下方，设备报的却是朝上为正，翻转放在了解码器里，后面消费的人拿到的就都是屏幕坐标的口径。

IRQ12 的线上来时，`irq12_thunk` 把 0x60 数据口的字节喂给 on_byte，我们把这个字节变事件的枢纽整段搬下来：

```cpp
// kernel/driver/mouse/ps2_mouse.cpp
void Ps2Mouse::on_byte(uint8_t raw_byte) {
    MouseSample sample{};
    if (!grammar_.feed(raw_byte, sample)) {
        return;
    }
    if (sample.dx != 0) {
        stream_.publish(kEvRel, kRelX, sample.dx);
    }
    if (sample.dy != 0) {
        stream_.publish(kEvRel, kRelY, sample.dy);
    }
    if (sample.left != left_) {
        left_ = sample.left;
        stream_.publish(kEvKey, kBtnLeft, left_ ? 1 : 0);
    }
    if (sample.right != right_) {
        right_ = sample.right;
        stream_.publish(kEvKey, kBtnRight, right_ ? 1 : 0);
    }
    if (sample.middle != middle_) {
        middle_ = sample.middle;
        stream_.publish(kEvKey, kBtnMiddle, middle_ ? 1 : 0);
    }
    stream_.publish_report();
}
```

解码没凑齐一份完整的报告就直接返回，凑齐了的才轮到发事件。位移只有非零的才有自己的记录，X 与 Y 各走 EV_REL 的 REL_X 与 REL_Y，报的是相对量，动了多少就报多少。按键报的是边沿，三个布尔成员存的是上一份报告的按键态，新报告与缓存不一样了才发 EV_KEY，三只键的号码是 0x110、0x111 与 0x112。每份报告的末尾都跟着 `publish_report()` 的同步字，报告收尾的节拍与 Linux 自家的设备是一致的，`input_config.hpp` 的注释写的就是这个口径，我们照着源码也核了一遍。头文件给 on_byte 的纪律是 IRQ 路径上整数运算加队列推入，慢动作是一处都不许有的。

## 初始化的纪律

init 的次序，头文件的注释排了四拍，与设备一问一答地把端口打开、默认值设好、报告放行，这些走完了 IRQ12 的线才开。落到代码上的话，`0xA8` 打开的是第二端口（kCommandEnableSecond），`0xF6` 设回去的是默认值（kAuxDefaults），`0xF4` 放行的是报告（kAuxEnableReports），然后轮到的是 arm_irq12，读回的是配置字节，竖起的是第二端口的 IRQ 门（0x02，代码里的 kConfigIrqSecond），写回去了这一步才算完，注册 handler 与开线是整个序列的最后两拍。整段的 init 都握着 IrqGuard，进门时存下的是中断的标志并关了中断，出门时按存下的样子恢复。注释给这个次序的理由，我们也照着念了出来：设备答上来的应答字节，要落在任何 handler 都还没出生的时刻，才不会落进谁的手里被误读成数据。它还要求键盘的 bring-up 已经走完了控制器的自检，两个驱动的出场次序就这样定下来了。

一问一答的礼仪住在 `controller.cpp` 的 SendAux 里。领起的是 `0xD4`（kCommandWriteAux），它告诉 8042 下一个字节走的是第二端口，跟在后面的是数据字节，接下来我们等的是应答，答上来的是 0xFA（kReplyAcknowledge）就算这一问成了，0xFE（kReplyResend）的意思是再来一遍，重试的预算是两次（kResendBudget = 2），其余的应答字节或者沉默的芯片都算这一问失败。SendAux 的注释把这层预算的来历写着，规格里的应答是毫秒级到的，预算取的就是这个量级，不用随手写的自旋次数。

失败的态度我们与键盘对照着记。init 里的任何一步答了假就返回假，装不上的设备不拦路，开机照常走它自己的路，引导序列里 pointer 这一步把返回值原样丢掉了（`static_cast<void>`）。键盘那一步答假可是要停机的，打字的设备装不上的话，后面的每一步都白搭，指针缺席倒是没人来追问。两台设备的待遇差别，[键盘驱动](keyboard.md)那一页里也记过一笔了。IRQ12 走的向量是 0x2C，算的还是线号加 0x20 的那套算式，与 IRQ1 的 0x21 走的是同一套算法。

## 数位板的绝对轴路

数位板的家我们从认家开始，本体的家在 `usb_tablet.hpp` 与 `usb_tablet.cpp`，解码的家在 `tablet_report.cpp`。跟 [进程与调度器](../proc/process_and_scheduler.md) 那页的 Scheduler 一样，它是 Meyers 的单例（实例是常量初始化的静态对象）。类驱动握着一个带类型的 UsbDevice，顺着描述符链找到自己的接口。USB 设备用一串描述符自报自己的家门，驱动顺着配置的链条一路读下去，读到的接口属于 HID 这个类别（人机接口设备），协议字节取的是 0，kHidProtocolNone 的注释说设备说的是自己的 raw report 协议（设备按自己声明的布局直接交报告），每份报告的坐标按绝对轴发布。

init 的路数我们跟着检查走。已就座的直接答真（ready_ 的短路在那里守着），然后轮到的是 enumerate 枚举设备，回来的端点（设备上的一条通信通道）要过我们这两道检查，地址的高位标的是方向为进，低位装的是端点号 1（两个合起来写就是 0x81），单包装得下的字节数至少是 5，过了的话再走的还有两步，一步走的是 set_configuration，另一步走的是 configure_interrupt_in。enumerate 没过的话直接就返回假了，后面的检查与配置里的任何一步不过，走的就是 detach 退场加返回假，头文件注释的态度与鼠标是一致的，装不上的设备不拦路，开机的路不会被它挡住。都过了之后等着的还有三件布置：configure_absolute(32767) 干的是头一件，它把流改成了绝对轴，两轴的量程定在 0..32767，QEMU 的 usb-tablet 报的就是这个量程。set_poll_hook 扛的是第二件，交给流的是抽干 XHCI 事件的钩子。轮到 interrupt_read 的是第三件，挂上去的是头一份报告的读取。成功的那一行日志 `[kern] usb tablet seated, absolute axes 0..32767` 也打得出来，开机的日志里我们认得出它。枚举用的端点描述类型叫 KeyboardEp，它的定义住在 xhci_layout.hpp，四个成员装的是配置值、端点地址、单包大小与轮询间隔，数位板借的是同一个形状，两类设备在端点的长相上没有差别。

钩子是数位板一侧最值得我们停下来看的布置，我们把它的用处说清。数位板的报告等在控制器的环形缓冲里，set_poll_hook 的注释把这层写明了，报告等抽干的设备把抽干交过来，读的人不用中断线也能拿到报告。poll_hook_ 的调用我们在 `event_stream.cpp` 里数过，read 与 poll 的门口各有一处，读者的每一次到访都会把控制器抽干一遍。

report_arrived 是报告的落点，我们把它的条件读一遍。条件一共设了两道，状态这道认的是成功或者短包（比约定长度短的包），长度这道认的是至少 5 字节，两道都过了的话才轮到解码：DecodeTablet 把 report[0] 的低三位读成按钮位，X 与 Y 依次占的是后面的四个字节，每根轴读的是 16 位的小端字，正好把那 5 字节的报告装完。流里进的是 EV_ABS，ABS_X 与 ABS_Y 占的是两条，value 装的直接就是坐标位置本身，按钮走的还是边沿比对的办法，0x110 到 0x112 的三只键与 PS/2 鼠标同名同码，报告的末尾由同步字收尾。函数的最后一行把 interrupt_read 重新挂上，下一份报告来了就再走一遍，读取的接力就这样一直续着。

## 两条路怎么汇进 event1

接线在 `kernel/fs/file_world.cpp` 的 FileWorld::init 里，我们搬下来翻：

```cpp
// kernel/fs/file_world.cpp
const bool kTablet = driver::UsbTablet::self().init();
RegisterInputDevices(
    driver::Keyboard::self().events(),
    kTablet ? driver::UsbTablet::self().events() : driver::Ps2Mouse::self().events());
```

我们把这段接线的意思翻出来，数位板的枚举就发生在这一刻，枚举成功了 event1 归它坐，失败了坐上去的就是 PS/2 鼠标，event0 的座位永远是键盘的。座位本身是 `kernel/fs/devfs/input_node.cpp` 摆下的，键盘的从号 kKeyboardMinor 是 64，指针的从号 kPointerMinor 是 65，所以 event1 的从号就是 65，主号是 13（Linux 的 evdev 用的也是这个数），坐的都是字符设备，总线上写的是 virtual，驱动名写的是 input，inode 的号从 0x2000 起按从号发。打开节点的时刻内核造一个带引用计数的 InputOpen，read 与 ioctl 走的是流，write 的回答永远是 kNotSupported，描述符关闭的时刻独占就自动松手了。

投进流的纪律两条路是同一套的，我们把它念一遍：publish 盖上节拍钟的戳，在自旋锁与 IrqGuard 的保护下推入，24 字节的记录形状与溢出时的 SYN_DROPPED 重建，[键盘驱动](keyboard.md)那一页讲过了。绝对轴多出来的一步是 publish 把 EV_ABS 的值记进轴的现值，EVIOCGABS 答的就是记下的值，量程是 configure_absolute 定下的那对 0..32767。键位图也是同样的道理，EV_KEY 的字更新的是 EVIOCGKEY 要读的键位图，键盘与指针各自记各自的。

## 与键盘驱动的关系

键盘与 PS/2 鼠标共用的是同一个 8042，线的分配却是一人一根，我们把两根线认一遍：键盘举的是 IRQ1，鼠标举的是 IRQ12，走的向量分别是 0x21 与 0x2C。配置字节里第二端口的 IRQ 门，键盘启动的时候把它按了下去，鼠标的 arm_irq12 再把它竖了起来，两个驱动各管各的时刻，谁也不替谁保管各自的门。有一层依赖是写在次序上的，鼠标的头文件注释写明了它的 bring-up 要求键盘的自检已经走完，引导序列里的 keyboard 步骤排在 pointer 步骤的前面，这样的依赖就落成了次序。

缺席的宽容程度，三台设备各有各的待遇。键盘一缺席的话，机器就停摆了。鼠标这一路倒是没人来追问。数位板要是枚举失败了，顶上 event1 座位的就是鼠标。数据的形状两边也不同，我们把两条路对一遍：鼠标的位移走的是 EV_REL，数位板的坐标走的是 EV_ABS，键盘的一侧没有位移也没有坐标，有的只是 EV_KEY。按键倒是鼠标与数位板两条路同码，0x110 到 0x112 的三只键同名同码，读到报告的人不用问设备是谁的。键盘的路报的是自己的键位码，0x110 到 0x112 的按钮号码跟它没有关系。独占这一问指针的流也答得上来，EVIOCGRAB 的问答是流自己答的，只是今天装了 grab 钩子的只有键盘一处（`keyboard.cpp` 的 attach），指针的独占记的是流内部的 owner_，控制台的字形面受不到它的影响，这也是顺理成章的事，指针本来就不喂控制台的字符。

## 源码位置

- `kernel/driver/mouse/ps2_mouse.hpp`、`ps2_mouse.cpp` — 设备本体、8042 第二端口的 bring-up、IRQ12 馈送
- `kernel/driver/mouse/mouse_packet.hpp`、`mouse_packet.cpp` — 三字节包的语法、包头签名与再同步
- `kernel/driver/usb/usb_tablet.hpp`、`usb_tablet.cpp` — 绝对轴数位板、端点检查、抽干钩子
- `kernel/driver/usb/tablet_report.cpp` — DecodeTablet 的小端解码
- `kernel/driver/ps2/controller.hpp`、`controller.cpp` — 8042 的命令词汇、SendAux 的应答与重试预算
- `kernel/driver/input/event_stream.hpp`、`event_stream.cpp`、`input_config.hpp` — 事件流、24 字节记录、绝对轴配置、poll 钩子与 ioctl 词汇
- `kernel/fs/devfs/input_node.cpp` — event0 与 event1 的落座、从号 64 与 65
- `kernel/fs/file_world.cpp` — 两条流的接线与 event1 的归属
- `kernel/init/init_sequence.cpp` — keyboard 与 pointer 两个引导步骤及其失败态度
- `kernel/arch/x86_64/irq_guard.hpp`、`vector_config.hpp` — 中断快照开关、0x20 起的向量分配
- `test/unit/test_input.cpp`、`test/kernel/test_keyboard.cpp` — 解码器的 host 测试与内核态里鼠标的出场
- `third_party/vesper/src/host/evdev_input.cpp` — 桌面读 event1 的那一头
