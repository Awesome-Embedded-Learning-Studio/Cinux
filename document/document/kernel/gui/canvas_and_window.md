# 画布与窗口

## 功能概述

如今咱们在源码树里找不着内核的图形栈了，画布与窗口咱们去用户态里找。

分工咱们摆在这里。画布与画笔是 visor 的家当，visor 是 vesper 自带的工具库。vesper 自己是仓库自带的桌面程序与其工具库的集合，仓库里占的是 `third_party/vesper` 这个子模块，visor 在它里面又占一层 `third_party/vesper/third_party/visor`（子模块说的是仓库里嵌着的另一个仓库），那半边的走读收在了 [Canvas 画布](../drivers/canvas.md) 那一页里。窗口这半边归 vesper 的桌面层，WindowFrame 是窗口的本体，Desktop 是桌面的根，Icon 是桌面的图标，Terminal 是终端的控件，它们全是 visor 控件树上的控件。整桌的落成例子是 `third_party/vesper/examples/fbdev_desktop.cpp`，`scripts/build-vesper-desk.sh` 把它静态编成了单个可执行，`scripts/create_ext2_disk.sh` 把产物装进镜像的 `/bin/vesper`。装了 desk 二进制的镜像里，同一个脚本还会把 `::respawn:/bin/vesper /dev/fb0 /dev/event0 /dev/event1` 直写进镜像的 inittab，开机就把桌面拉了起来，以 root 的身份跑，会话档是不挑的，进程退了 init 还会照 respawn 这档把它拉回来。`init.d/desktop` 走的是 gui 会话档的这一路，由 inittab 的 `::once` 调，开机进哪一档由 `CINUX_SESSION` 挑（挑档的说明在 [桌面与图标](desktop_and_icons.md) 那一页），gui 的时候它才动手，命令行写的是 `/bin/vesper /dev/fb0 /dev/event0 /dev/event1`，身份换成了 cinux，event0 的座位坐的是键盘，event1 坐的是指点设备。

这一页的读法跟着窗口走。咱们从窗口的本体读起，再看桌面怎么编排窗口与焦点，然后看事件怎么找到窗口，末了看窗口的像素怎么跟着条带与脏矩形落到 `/dev/fb0`。深读的专页同目录里还有三页，咱们照列在这里：[桌面与图标](desktop_and_icons.md) 摊开讲的是桌面根、图标与启动器，[Terminal 终端](terminal.md) 分开走控制台、伪终端与 PtySession（shell 坐在伪终端上的会话件）三摊，[窗口管理器与事件系统](wm_and_events.md) 咱们跟着事件从内核一路走到桌面。

## 窗口的本体：WindowFrame

WindowFrame 的定义在 `third_party/vesper/src/ui/window_frame/window_frame.hpp`。类注释给它的定位是三样东西的组合，咱们一条条认：一条能拖的标题带，一个收回窗口的关闭盒，一块装一个控件的内容区。窗口自己也是 visor 控件树上的一个控件，带与轮廓是它在 `on_draw` 里亲手画的像素，一圈一像素的描边走主题的 Border 颜色角色（角色色是主题里的抽象颜色，主题负责把它们折成具体的像素），圆角取的也是主题的，一套主题把四个角定成什么样，窗就画成什么样：fbdev 桌面接的这套主题给四个角的圆角都只有 2 像素，窗带的就是一道很浅的圆角。源码注释把通例讲在了头里，咱们照着读，角是方的主题画出来的才是方窗。

关闭盒是一个真的 Button 子控件，尺寸定的是 12 像素见方，贴着带的右角、往里缩 2 像素，标签是标题字体的一枚 X。咱们看它为什么描边不填实，构造函数旁的注释讲得直白：实心按钮涂的是 Accent（主题里的强调色），跟活动带的墨是同一支，涂上去就融进了带里，空心的框在带的两种状态下都读得出来。

桌面的布局数字集中在 `third_party/vesper/src/ui/style/style.hpp` 一个文件里，咱们挑窗口要用的几个认一遍。标题带的块头是 16（边框行算在内），关闭盒的尺寸是 12，内容四周的留白是 4，标题离左缘的距离是 8，底栏的高度是 20，图标槽的框是 56 见方。文件头的注释说，控件读的只有这些数，自己的一个都不带，改密度要动的只是一个文件。内容区的算法不在这个文件里，window_frame.cpp 的 `content_area()` 把它定好：带底之下留的是一圈留白，边框那 1 像素算进了留白里，`content_area()` 返回的就是这块矩形。

拖动是这扇窗的头一件本事，做法是捕获加一次减法的组合，咱们把这两半分开看。按压落在了带上的时候，`drag_offset` 把指头与窗口原点的距离记了下来，此后的每一次 move 都拿这个偏移把原点重推了出来。缰绳是构造时递进来的 `desktop_bounds`，拖动被夹在这块工作区（窗口能待的那块矩形）里，fbdev 桌面递进来的工作区是整屏减去底栏的高度。夹的理由注释写着，标题带是窗口唯一的把手，一扇能跑出桌面的窗，会把带子拖到够不着的地方。

```cpp
// third_party/vesper/src/ui/window_frame/window_frame.cpp（节选）
void WindowFrame::on_pointer_move(PointI at) noexcept {
    if (!dragging_) {
        return;
    }
    PointI next{at.x - drag_offset_.x, at.y - drag_offset_.y};
    const int slack_x = std::max(desktop_bounds_.size.w - bounds().size.w, 0);
    const int slack_y = std::max(desktop_bounds_.size.h - bounds().size.h, 0);
    next.x = std::clamp(next.x, desktop_bounds_.pos.x, desktop_bounds_.pos.x + slack_x);
    next.y = std::clamp(next.y, desktop_bounds_.pos.y, desktop_bounds_.pos.y + slack_y);
    set_position(next);
}
```

按下的次序咱们也看一遍。按在带或框上的按压要是没有被孩子截走，落到了窗口这一层，`on_focus_` 的回调就在带测试（标题带矩形的 contains 判断）之前响了起来，注释说这跟窗口管理器见到按压就递焦点的做法一致。带子的高度把顶部边框行也算在内，按着顶框开始拖也是允许的，注释说这样读起来抓的就是窗框。只有按在带上的时候才会开始拖，因为关闭盒与内容各自把自己的按压截在了到达窗口这一层之前。关闭盒按下去走的是 Button 的激活路，桌面接过 `set_on_close` 的时候，桌面给什么答案，什么就算关闭，没接的时候走默认的 `close()`，`set_visible(false)` 把窗口藏了起来，状态与内容都留在原来的地方，注释补了一句，藏起来的窗在自己盖过的地方交还像素，地面重新露了出来，下次 `set_visible(true)` 的时候原样回来。

窗口的亮与素是 `set_active` 画出来的状态：活动的那扇戴 Accent 的带，不活动的走素底，标题字的墨跟着换 OnAccent 或者 Foreground。`set_active` 在状态没变的时候是空操作，注释说桌面在每一次按压时都可以放心地调它，这一点咱们在 fbdev_desktop 里见到了，`on_frame_focus` 在两扇窗上各调了一次。

移动与改尺寸走的都是 `Widget::set_bounds` 一条路，咱们把这笔安排读明。旧框与新框一起报给了脏矩形收集器，孩子跟着 `layout_children` 重排各自的框，窗口自己就不用另报一笔了。注释把道理讲明了，孩子的报告本来就落在窗口新旧两框的范围里，漏是不会漏的，多出来的那几笔小框是给条带重放省工的，框小了，重画的条带也就少了。

## 桌面这一层：Desktop 与图标

Desktop 的定义在 `third_party/vesper/src/ui/desktop/desktop.hpp`，咱们把它当 visor 控件树里的普通一员来读。孩子按加入的次序画，命中测试是倒着走的，晚加进来的窗口浮在早加进来的上面，层次就是子表里的次序，平台规则是直接从 Widget 继下来的。桌面自己画三样：整幅的底子，一条沿底边、高 20 的面板（左侧是桌名的横幅标签，右侧是留给调用者放状态字的地方），还有可选的墙纸，一张单色位图画在桌面原点、垫在了所有孩子的下面。墙纸是借进来的视图，注释提醒的是存储要比桌面活得久，出了桌面框的部分安静地裁掉。

fbdev_desktop 的装配段咱们整段读，出生在这里的还有两个窗口、两个图标与一块欢迎面板，开画之前两扇窗都收了起来，桌面上站着的是底栏与图标，见窗要等的是双击。

```cpp
// third_party/vesper/examples/fbdev_desktop.cpp（节选）
Desktop desktop{screen_bounds, "VESPER", vesper::fonts::title::kTitleFont, desk_theme};

WindowFrame welcome_win{RectI{.pos = PointI{160, 50}, .size = SizeI{.w = 270, .h = 130}},
                        "About", vesper::fonts::title::kTitleFont, work_area};
WindowFrame term_win{RectI{.pos = PointI{20, 40}, .size = SizeI{.w = 600, .h = 400}},
                     "Terminal", vesper::fonts::title::kTitleFont, work_area};
Terminal term{term_win.content_area(), vesper::fonts::term_font::kTermFont};

term_win.set_on_focus(&on_frame_focus, &term_opener);
term_icon.set_on_activate(&open_window, &term_opener);
```

Terminal 的内容是一块反色的屏幕（亮底，字与光标打成暗的洞），背后的 `/bin/sh` 跑在 pty 上（pty 是伪终端的一种，给了 shell 一条像真终端的通道），pty 的窗口尺寸设成终端的行列数，注释说行编辑器照着屏幕自己的格子重画，尺寸是例子明明白白设下的，不走猜宽度的默认。面板右下角的号牌写着 MONO 1，用的是界面字体。咱们把两扇窗的尺寸也记下（About 是 270 乘 130、Terminal 是 600 乘 400），都是例子定的，fbdev 桌面用的这套控件里没有排布窗口的活。

图标是一座一槽的，命中区是 56 像素见方的一块，里面垫的是 24 像素的双层墨画与一行标题。单击做的是选中，戴圈的同一时刻只有一枚，开窗要的是双击。这两步的分开是 icon.hpp 注释里讲明的故意，咱们把它转述在这里：选中发生在第一下按压的时候，开窗等的是一双凑齐。开窗的含义由桌面接线，`set_on_activate` 递进去的是一个函数加一个上下文。注释交代接线的口气是单向的，窗口的存在，图标自己是不知道的，反过来的那半句注释里没有写。

焦点环是例子手写的一小段编排，咱们把它的家当认一遍。fbdev_desktop 拿来的 Opener 结构装了三样，装进去的是自己的窗口、对面那扇、要接键盘的控件。双击图标的时候，`open_window` 干的头一件事是 `set_visible(true)`，接着 `raise()` 把它抬到了子表的头上，然后两扇窗的 `set_active` 一真一假，需要键盘的再把 `set_focused` 递给了内容。一次戴亮带的只有一扇窗，这个环是例子手写的，焦点管理器库里是没有的。

visor 的控件层另有一条真焦点，咱们把它与手写的环分开记。一棵树同时至多有一个持着键盘焦点的控件，画的时候给它描一圈一像素的环，Tab 沿画的次序把焦点递了下去，`advance_focus` 可以被圈在一个焦点域的范围里，注释举的例子就是窗口子树，Tab 是出不了这扇窗的。两条路的分工是两回事，fbdev 桌面把两样都用了：窗口谁亮走手写的环，Terminal 里的 Tab 走控件层的，实际上被截走了，事件的路由一节说这个。

## 事件的路由：InputRouter 与捕获

visor 的 ui 层里，事件的路由住在 `third_party/vesper/third_party/visor/include/visor/ui/input_router.hpp`，按压的所有权归 InputRouter 管。咱们把这套规则完整转述一遍：PointerDown 落在了谁的身上，之后的每一次 move 与那一次释放就都归了谁，指针不管跑到哪儿都是跟着的。类注释把这件事的用处讲成两半，按空了的释放不会丢，释放落在按住的控件外面也送得到，拖动有了根基，按住的控件出了界之后还听得见 move。所有权是跨轮存在的，与脏矩形收集器一样是树旁边一份调用者所有的状态。

没有按住的时候，move 按命中测试（按坐标找出该收这一下的最上层控件）走，滚轮走的也是悬停词这一路（hover word 是注释里的叫法，说的就是不建立按压所有权、只认指针当前的位置送件），送到指针底下的最上层控件，按压的所有权是不碰的。键盘里只有 Tab 是命名的键码，沿树走的是焦点域，其余的键码原样递给焦点持有者，控件自己比对键码——`bindings/events.hpp` 收的是六种事件的汇总，键码一份一份列在它 include 的 `bindings/events/key_words.hpp` 里——不认识的忽略。这些去向咱们都对着 input_router.hpp 的注释核过。

双击的识别也住在这里。咱们把它的条件完整记下：同一控件上两次主键的按下与释放，两次的间隔不超过 500 毫秒，每个轴的落点差不超过 4 像素，第二下释放走完自己的 `on_pointer_up` 之后再补一发 `on_double_click`。单击的脚步从不变慢，每一对的按下与释放都立刻生效，双击只是额外的一下手势。认人认的是控件 id，地址是不作数的，注释说控件销毁之后的地址可能被新的控件复用，认地址会把旧控件的说法安到新控件头上。时钟没接的时候，关掉的是整个手势，注释连这个都交代了。因为 now_ms 的零也是时间轴上真实的一刻，当不得钟缺席的记号，没有钟的时候零顶替不了钟，拿它去比两次按下的间隔只会比出错的长短，所以路由器宁可不比，手势就这么整个关掉了。

fbdev 桌面把这套语义用在了三处，咱们一处一处对。WindowFrame 的拖动靠捕获，Icon 的双击靠路由器的配对，还有一处细的，fbdev_desktop 的事件泵里 `Terminal::capture_event` 抢在事件交给 visor 之前，把聚焦状态下的 Tab 截了下来。为什么这么接，咱们的读法是：屏幕控件吃的是打字，Tab 对它该是一下递给里头程序的按键，放它走控件层的焦点漫游，这一下就被挪去递焦点了，所以在交给 visor 之前把它截住。

浮动的一层也在 ui 层里，咱们把 PopupLayer 的收养与退场读一遍。它是挂在树的根上的全屏透明底座，`open()` 收养的是一个浮置的子树，然后把自己升到了最上的位置再显形。内容用绝对的画布坐标摆自己，不受逻辑上拥有它的窗口的框约束，挂根就是为了这个，注释说菜单能伸出窗口边靠的就是这一下。层盖住的是全屏，层外的按压被它自己吃掉的同时顺手收掉了弹层，底下的控件是听不见这一下的。代价注释也算过了：开与关各报的一笔是整屏重画，第一版的定价，正确性排在了省工前面。

## 窗口的像素怎么交出去

画是重放出来的。visor 的 ui 层有一条渲染原语 `render_window`，一棵树在一个场景窗口（这一轮要画的矩形）下整树重放，缓冲装的就是这个窗口，画完了就交给 host。run_loop.hpp 的注释把全帧、条带、脏矩形说成同一条原语的三种穿法，咱们把三种穿法排开：全帧是覆盖画布的那个窗口，条带重建是同一棵树在每条条带的窗口下重放一遍，脏矩形这一穿重画的是收集器挑出的那些窗口。重放合法的根由注释也写了，画只读的状态、什么都不碰，同一棵树在每个窗口下落下的像素都一样。

条带剖面与脏矩形收集器的细节（宽 4096、高 16 的条带画布，容量 8 的收集器，重叠的矩形是当场合并的，装不下的时候折成一个包围盒）在 [Canvas 画布](../drivers/canvas.md) 那一页走过了，这里就不再重复了。fbdev_desktop 的主循环走的是脏矩形的经济路线，咱们把它的节奏读出来：poll 的节奏是睡 15 毫秒，等的是输入与 shell 的输出，醒了把事件交给 `run_dirty`。`run_dirty` 的节奏有一条写进注释的规则，一轮消费的键事件至多一个，按下与松开各占的是一次完整的重画，注释说这个节奏是结构性的，宿主的手感与时钟都是不借的，指针事件是随到随清的。开画的第一笔是例子自己 push 进收集器的整屏，`run_dirty` 只管后面欠下的，没欠像素的一轮是什么都不交的，屏幕上站着的还是上一轮的画面。

提交走的是 HostTable 的 flush 座位。HostTable 的定义在 `third_party/vesper/third_party/visor/include/visor/bindings/host.hpp`，它是 visor 的宿主接线表，宿主把自己要接的函数填进去，库的本体只经它调用。fbdev 桌面接的 `fbdev_flush` 把光标补画上去之后，整段的行字节 memcpy 进 mmap 到手的显存，这半边的接缝（显存怎么映射、行距怎么认、控制台怎么让路）是画布那一页的主场，咱们不重复。光标本身是树外的覆盖件 SoftwarePointer，注释特意说它住在桌面树的外头，命中测试是不参与的，指针的每次 move 只改它自己的 bounds。

层次与合成的分工咱们也交代一句。fbdev 通道是没有显示服务进程的，合成器也是没有的，fbdev_desktop.cpp 文件头的原话是 “no display server, no compositor, one mmap and two device nodes”。多扇窗叠着的层次是 Desktop 子表里的次序加 `raise()`，合成的活是由整树重放担着的。vesper 另有一条伺候 Wayland（一类显示服务的协议）宿主的合成器通道，家安在了 `third_party/vesper/src/compositor`，CinuxOS 的镜像里装的是 fbdev 通道，合成器那边的路数这一页就不展开了。

窗口没有自己的缓冲。visor 的缓冲装的恰好是正在渲染的那个窗口或条带，画布页讲过这样安排的道理，窗口的样子因此全是重放出来的，藏起来的窗是不占任何像素的。状态的态度咱们也记下，window_frame.hpp 的注释写着 “two states, painted, not inferred”，哪扇窗亮、哪枚图标选中、谁有键盘的答案，都是画出来的状态，焦点管理器不是窗口自己拥有的东西，桌面说了哪扇亮，哪扇就跟着亮了。

## 源码位置

- `third_party/vesper/src/ui/window_frame/window_frame.hpp`、`window_frame.cpp` — WindowFrame 本体：标题带、关闭盒、拖动与焦点接线
- `third_party/vesper/src/ui/desktop/desktop.hpp`、`desktop.cpp` — Desktop 桌面根：底栏横幅与墙纸
- `third_party/vesper/src/ui/icon/icon.hpp`、`icon.cpp` — 桌面图标：选中与双击开窗
- `third_party/vesper/src/ui/style/style.hpp` — 桌面的布局常量
- `third_party/vesper/apps/terminal/ui/terminal/terminal.hpp` — Terminal 控件与 `capture_event`
- `third_party/vesper/examples/fbdev_desktop.cpp` — fbdev 通道的整桌装配与主循环
- `third_party/vesper/third_party/visor/include/visor/ui/run_loop.hpp` — render_window、render_strips、run_dirty、run_once
- `third_party/vesper/third_party/visor/include/visor/ui/dirty_regions.hpp` — 脏矩形收集器
- `third_party/vesper/third_party/visor/include/visor/ui/input_router.hpp` — InputRouter 与 dispatch_event
- `third_party/vesper/third_party/visor/include/visor/ui/popup_layer.hpp` — PopupLayer 浮层
- `third_party/vesper/third_party/visor/include/visor/ui/widgets/widget.hpp` — Widget 基类：raise/lower、焦点与报脏
- `third_party/vesper/third_party/visor/include/visor/bindings/host.hpp` — HostTable 宿主接线表：flush、poll_event、clock_ms、spawn 四个座位
- `kernel/fs/devfs/fb_node.cpp`、`kernel/fs/devfs/input_node.cpp` — `/dev/fb0` 与 event0、event1 的座位，走读在 [Canvas 画布](../drivers/canvas.md)
- `user/rootfs/buildroot/overlay/etc/init.d/desktop`、`scripts/build-vesper-desk.sh`、`scripts/create_ext2_disk.sh` — 桌面的拉起（inittab respawn 与 gui 会话两条路）、静态编链与 `/bin/vesper` 的落座
