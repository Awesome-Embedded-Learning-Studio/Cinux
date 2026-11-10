# 桌面与图标

## 功能概述

这一页我们看 Cinux 的桌面与图标如今在哪里、怎么跑。内核的 GUI 在 2026 年 10 月 31 日的迁移里整体退场了，删除的动作发生在提交 49ddfb0，旧的 32x32 位图图标、DesktopIcon 与桌面装配一起下了车，图形栈搬进了用户态的桌面程序 vesper，仓库里占的是 `third_party/vesper` 这个 submodule（仓库里套着的另一个仓库）。今天图形会话里的桌面（gui 会话是这些会话里带桌面的一档，开机进哪一档由 CINUX_SESSION 的值挑），就是 vesper 的桌面例子 fbdev_desktop 静态链接出的 `/bin/vesper`，桌面的根是 vesper 自带界面库 visor 之上的 Desktop 控件，图标的角色归 Icon 控件，批量摆图标与生程序（spawn 这个词说的是替新程序点火开工）的活还配了一份 Launcher 注册表，三层的单元用例在 vesper 的测试里都能找到。

我们把这页的路线排一下。开头的部分看 gui 会话怎么把 `/bin/vesper` 送上屏幕，中间我们分开读桌面根、图标与启动器这三层，末尾跟着 fbdev 桌面例子的 main 把装配与主循环走一遍。像素怎么画、显存怎么交到用户进程的手里，我们在 [Canvas 画布](../drivers/canvas.md) 那页读过，它已经按新口径重写过了。`kernel/gui` 底下其余的三页在本批也重写完了，它们的名字是 canvas_and_window、terminal 与 wm_and_events，画布与窗口走的是 visor 画布那一套，终端走的是内核文本台加 PtySession（vesper 那头的伪终端会话件），窗口管理器与事件系统写的是退役与事件链，名字我们照列在这里、链接等站点整理时再挂。旧页里的 Shell 与 Calculator 图标在新桌面里没有对位物，顶替它们的是 WELCOME 与 TERMINAL 两个图标，这个对照我们在图标一节里展开。

## 拉起链：gui 会话怎么把桌面送上屏幕

桌面的出场要靠一个环境变量来请。CINUX_SESSION 的值不走内核的命令行，QEMU 通过 fw_cfg（客户机在运行中读宿主电脑配置的一条通道）的 `opt/cinux/init-env` 项把它递进来，内核只负责把这项有界的初始环境交给 `/sbin/init`（busybox 的 init，读的是 `/etc/inittab`），这套安排的说明在 `user/rootfs/buildroot/README.md` 里写着，我们照着它转述。`inittab` 给它的行是 `::once:/bin/sh /etc/init.d/desktop`，sysinit 是 inittab 里排在最前的那类系统初始化条目，init 跑完了它，就来执行 desktop 的那一行。脚本认的条件只有一个，CINUX_SESSION 等于 gui 的时候，它以 cinux 用户的身份 `exec /bin/vesper /dev/fb0 /dev/event0 /dev/event1`，三个参数的顺序是帧缓冲、键盘流、指点设备流，`/dev/event0` 与 `/dev/event1` 两条流的落座归内核的输入设备节点管，挑选的规则我们在画布那页读过。

环境项缺省或者不是 gui 的时候，脚本就直接退出了，串口的登录照常，`inittab` 里 `console::respawn:/bin/su - cinux` 的那一行把文字会话扶起来，机器就停在命令行里了。二进制怎么静态链接、怎么坐进镜像的 `/bin/vesper`，这些我们在画布那页“内核侧”一节的末尾读过，那里有一段构建的交代，这里就不重复了。vesper 自己还有一个宿主电脑上的 VM 冒烟通道，设备名走的是 Linux 的裸命名 `/dev/input/event*`，Cinux 镜像给的是 devfs（内核的设备文件层，设备以文件的样子坐在 /dev 底下）的 event0 与 event1，两种叫法接的都是同一个桌面，参数本来就是命令行传的。

## 桌面本体：Desktop 控件

Desktop 的定义在 `third_party/vesper/src/ui/desktop/desktop.hpp`，头文件的注释给它的定位很实在：桌面是 visor 的控件树里一个普通的 Widget，平台的行为全部继承，我们自己不用再发明一遍，孩子按加入的顺序画、命中测试倒着走。命中测试说的是指针按下去的时候，控件树从画在最上面的孩子往回找，找的是接得住这一按的控件。所以窗口只要排在面板之后加入，就浮在一切早加入的东西的上面，这个次序就不用桌面自己操心了。

它自己画的地面、壁纸与面板，`on_draw` 里的次序就是它们仨的次序，我们对着代码看：

```cpp
// third_party/vesper/src/ui/desktop/desktop.cpp
void Desktop::on_draw(const visor::ui::Painter& painter) const noexcept {
    painter.fill(bounds(), ColorRole::Background);
    if (wallpaper_ != nullptr) {
        painter.draw_bitmap(bounds().pos, *wallpaper_, ColorRole::Foreground, 1);
    }
    // The panel is the foreground ink, so the banner label (drawn as a
    // child on top) takes the background role and reads as a cut-out.
    painter.fill(panel_bounds(), ColorRole::Foreground);
}
```

整幅边界填的是背景角色的墨，墨是主题层的抽象颜色，落进画布的时候才折成像素，这个折算我们在画布那页读过。挂了壁纸就把壁纸按前景墨画在地面上，末了把底部那道全宽、`style::kPanelHeight`（20 像素）高的面板条填成前景色。注释交代了面板上那个字的读法：横幅 banner_ 是个用背景墨的 Label 孩子（Label 是 visor 里显示一行字的控件），面板画的是前景，名字读起来就是面板上镂出来的字。桌面的名字在构造的时候给，fbdev 桌面给的是 VESPER，横幅从 4 像素的内容缩进起笔，缩进的常量是 `style::kContentPadding`，落点在面板条的居中位置。面板的右端留了 `panel_bounds()`，宿主程序（把 visor 嵌进去跑的那个程序）想放状态字的话可以自己往里锚一个 Label，fbdev 桌面的 MONO 1 标签摆的正是面板右端的位置。

壁纸是个借来的视图。`set_wallpaper` 收的是一个 MonoBitmap 的指针（visor 的单色位图、一位对着一像素的单色点阵），注释把它的纪律写得很清楚：存储的寿命要盖过桌面，通常是一张解码好的 PbmImage（PBM 格式的单色位图文件），跟主题的指针、字体的指针同一条纪律，越出桌面边界的部分静默裁掉。换壁纸的那一下 `invalidate` 整个桌面，理由注释也给了：地面的像素变了，整个桌面都欠了一轮重画，上面的窗口盒在同一轮的重画里盖回自己的那层。我们看过两个例子的装配，两边的壁纸都没接，这个挂点由 `desktop_tests.cpp` 的用例守着。

## 图标：Icon 控件与它的资产

Icon 的定义在 `third_party/vesper/src/ui/icon/icon.hpp`，我们拿它管的事来开个头：它管的是一块画加一行标题加一个双击的动作，画与标题落在同一个居中的块里。画是分层的，每层是一个 24x24 的单色位图加一个墨色角色，层叠在同一个居中的块上，后画的压在早画的身上。单位图的构造也有，整块的画用一种墨、标题用正文的墨。命中靶的尺寸在 `style.hpp` 一处，两个常量 `kIconSlotWidth` 与 `kIconSlotHeight` 的值都是 56 像素，注释说这 56 见方的格子里装的是画、标题和它们周围的呼吸。

像素的资产住在 `assets/icons/desktop_icons.hpp` 里。对照旧世界的话，图标换了一整代：旧桌面的图标是 32x32 的 constexpr 像素数组，名字挂的是 Shell 与 Calculator。这一页改成了 24x24 的双层单色位图，名字挂的是 WELCOME 与 TERMINAL。fbdev 桌面的两个图标各自都是两层，窗口的图标是金框叠着墨色的玻璃，终端的图标是灰身叠着金色的字。我们看资产文件的头注释，它把这么安排的理由讲明了：双层让彩色的图标继续走单色位图的管线，字体、壁纸、`draw_bitmap` 走的都是同一条路，彩色的画就是同一位图穿着不同的墨，不用引入新的资产格式。每个字节的旁边注释着点阵画，改动一张图标的时候，diff 里看到的就是点的进出。

有一处安排值得我们单独念，因为它背后的考虑非常具体。标题没有宿在 Label 孩子的身上，注释写的原因是命中测试返回的是最深的孩子，标题若是一个 Label 的话，按在标题上的那一下就归了 Label，一个图标散成了两个命中目标，输入路由器 visor::ui::InputRouter 的同控件配对就断了，双击就打不开了。所以标题就在 `on_draw` 里自己画，命中目标整个落在图标的身上。

旧世界的图标点一下只记下动作，等到下一个轮次到了才消费。这一套 IconAction 与 pending_icon_action 的排队，退场了，我们把接管的位置看清楚：双击的回调如今由 visor 的 InputRouter 接管，图标手里拿的是 `set_on_activate` 接的函数指针。

我们把手势的两拍分开读。第一拍的动作是选择，走的是 `on_pointer_down`，桌面的 Selection 只让一个图标戴环，换环的时候把旧的摘下来给新的戴上。第二拍是双击的完成，时间窗与容差的判断都归路由器，两次按落在同一个图标的身上，`on_double_click` 就送回来了，图标调它接的 `set_on_activate` 回调。图标不知道窗口的存在，注释的原话说得明白，打开具体指的是什么，由桌面接的回调说了算，图标不管窗口的事。没接回调的图标照样画它的墨，双击就落进安静里了，跟没接线的按钮是同一个姿态。选中环的画法也有讲究，`icon.cpp` 的注释自己讲了理由：环画的是四条一像素的边框线，用的是 Border 角色，底下的桌面与壁纸从环里透出来，一块实心的板会把图标底下的画面切出一个黑洞，注释拿这个对比讲了留环的理由。选中环的那段源码我们抄在下面：

```cpp
// third_party/vesper/src/ui/icon/icon.cpp（节选）
if (selected_) {
    // A one-pixel ring, not a plate: four edge runs keep the desk and
    // any wallpaper visible inside the slot. A filled plate would cut a
    // black hole in the picture under the icon.
    const RectI box = bounds();
    const SizeI top{.w = box.size.w, .h = 1};
    painter.fill(RectI{.pos = box.pos, .size = top}, ColorRole::Border);
    painter.fill(RectI{.pos = PointI{box.pos.x, box.pos.y + box.size.h - 1}, .size = top},
                 ColorRole::Border);
    const SizeI side{.w = 1, .h = box.size.h};
    painter.fill(RectI{.pos = box.pos, .size = side}, ColorRole::Border);
    painter.fill(RectI{.pos = PointI{box.pos.x + box.size.w - 1, box.pos.y}, .size = side},
                 ColorRole::Border);
}
```

## 启动器：Launcher 注册表

Launcher 的定义在 `third_party/vesper/src/ui/launcher/launcher.hpp`，我们把它的两半分开读。数据的一半是 LaunchEntry：标题、层的数组、path 与 argv，全是借来的指针，注释说桌面的程序跟字体一样是静态的数据，注册表的复制它不做，argv 留空的意思是只有 path，argv[0] 按生程序的请求原样带着。管理的一半是 Launcher 本体：构造的时候给列顶的座位，`add` 的一行进一个程序，`slot_for` 按图标样式的尺寸往下逐格步进，算出的是第几个座位摆在哪里。容量的上限是 `kCapacity = 8`，列满之后 `add` 答的是假、什么都不改，注释的立场是明着拒绝，悄悄的覆盖它不做。

这层注册表的用武之地我们要照实交代：fbdev 的桌面，也就是 Cinux 镜像里跑的那一档，Launcher 在它的装配里没有出场，两个图标是 main 里手工摆的，双击接的是打开本地窗口。走 Launcher 的是 `compositor_desktop.cpp`，宿主电脑上的 Wayland 合成器例子，它把 TERMINAL 的一行 `add` 进注册表，按 `slot_for` 摆出整列的图标，双击的反应把条目的 path 与 argv 交给生程序的座位。注释给这个形态的定位落在复用上：要添一个程序的话，加一行表就够了，桌面的代码一个字不用动。fbdev 档哪天要走注册表的路，件已经在库里了，行为有 `launcher_tests.cpp` 的用例守着。

## 装配与循环：fbdev 桌面的桌面

装配就是 `fbdev_desktop.cpp` 的 main 按顺序走一遍，它就是 Cinux 镜像里 `/bin/vesper` 的本体，我们跟着它的次序看。打开了帧缓冲与两条输入流之后，它组起的是这样一棵树：Desktop 的根（横幅 VESPER）、面板右端的 MONO 1 标签、两个图标（WELCOME 的座位在 (16,16) 的位置，TERMINAL 的在 (16,84) 的位置，座位都是 56 的见方）、两个 WindowFrame 的窗口（About 与 Terminal），终端窗口的内容是一个挂了 pty（伪终端的意思是，shell 在另一头把它当作真终端用的那对设备）的终端控件，pty 里跑的是 `/bin/sh -il`。图标接好了回调：双击 WELCOME 的话，About 窗就 `set_visible`、`raise` 并标成活动的窗，双击 TERMINAL 开的是终端窗，键盘的焦点交给了终端。两个窗口起步都是收起的，屏幕上剩下的就只有桌面与图标了。光标的位置在树外，是一个软件画的 SoftwarePointer，12x18 的像素箭头，注释给的理由是把它放在桌面树的外面，指针就永远不会拦截树上的命中。

主循环一轮的节奏，我们跟着代码读一遍：一轮 poll 三个描述符，键盘、指点设备、pty 的 master 端，超时的值是 15 毫秒，谁有货的就由谁叫醒桌面，全都安静的话这一轮就睡满了。醒来的活交给 visor 的 `run_dirty`，脏区重画的那一套我们在画布那页读过，这里就不重复了。shell 有话的一轮再补一次 `pump`，把 shell 的输出灌进终端。键盘在 `fbdev_poll_event` 里排在指点设备的前面，注释说打字的一轮让键盘出，指针的话在它的节点里等下一轮。shell 退场之后的 master 端会永远挂着断开，处理的办法注释也写了：那个座位就从 poll 的名单里摘出去，负的描述符 poll 自己忽略，不然每一拍都会在同一个挂断上白白地醒。

退出没有关闭的按钮可点，注释把原因说了：裸的帧缓冲上没有桌面能拥有的关闭钮，宿主电脑上的 Ctrl-C 也行。收场的安排两侧要分开说：vesper 自己的冒烟 VM 里，构建脚本生成的 `/init` 脚本把桌面跑在 timeout 底下（timeout 的默认值是 20 秒，改 FBDEV_TIMEOUT 的值就能调），到点由 timeout 的 SIGTERM 收场（这个 /init 是内核用 rdinit=/init 启的冒烟脚本，与 Cinux 的 /sbin/init 不是同一个东西）。trace 的输出跟着 console=ttyS0 的串口走，宿主电脑上的 smoke.sh 把串口收进 serial.log，等的是 GUEST_RUN_DONE 一类的记号。Cinux 客机侧走的是 ::once 的直拉，脚本与 overlay 里找不到发 SIGTERM 的环节，桌面的收场对我们来说还是一件待核实的事。想看桌面活着没有的话，我们把 `VESPER_FBDEV_TRACE=1` 打开就行，它是裸通道上唯一的调试口，每个事件与每次 poll 的醒都打到 stderr，空闲的时候每 320 轮还会打一行心跳。

## 源码位置

- `third_party/vesper/src/ui/desktop/desktop.hpp`、`third_party/vesper/src/ui/desktop/desktop.cpp` — Desktop 控件：地面、壁纸挂点、面板与横幅
- `third_party/vesper/src/ui/icon/icon.hpp`、`third_party/vesper/src/ui/icon/icon.cpp` — Icon 控件：层、双击、选中环
- `third_party/vesper/src/ui/launcher/launcher.hpp`、`third_party/vesper/src/ui/launcher/launcher.cpp` — LaunchEntry 与 Launcher 注册表
- `third_party/vesper/src/ui/style/style.hpp` — 面板高、图标座位、标题间隙这些布局数
- `third_party/vesper/assets/icons/desktop_icons.hpp` — 24x24 双层图标位图资产
- `third_party/vesper/examples/fbdev_desktop.cpp` — Cinux 镜像里桌面的本体：装配、回调与主循环
- `third_party/vesper/examples/compositor_desktop.cpp` — Launcher 注册表的使用处
- `third_party/vesper/tests/ui/desktop_tests.cpp`、`third_party/vesper/tests/ui/icon_tests.cpp`、`third_party/vesper/tests/ui/launcher_tests.cpp` — 三层各自的单元测试
- `user/rootfs/buildroot/overlay/etc/inittab`、`user/rootfs/buildroot/overlay/etc/init.d/desktop` — gui 会话的拉起
- `user/rootfs/buildroot/README.md` — fw_cfg 传递 CINUX_SESSION 的说明
