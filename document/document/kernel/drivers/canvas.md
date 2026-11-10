# Canvas 画布

## 功能概述

画布管的是绘图这一件事。它把一块按行排布的像素缓冲拿在手里，让控件树的画笔在它上面落点、填充、描字，再把画好的整行字节交出去。这一页我们把画布的现状讲清，因为画布已经不在内核里了：内核曾经自带一份自己的画布与一整套 draw 接口（旧路径 `kernel/drivers/canvas.hpp/cpp`），2026 年 10 月 31 日的重构迁移把它连着内核 GUI 一起删掉了，这件事的提交号是 49ddfb0。绘图搬进了用户态，如今住在桌面 vesper 自带的工具库 visor 里，仓库里占的是 `third_party/vesper` 这个 submodule。内核留下的只有显示出口，我们看到的是 framebuffer 驱动，加 devfs 里的 /dev/fb0 座位两样。

这一页的读法跟着像素走。我们从画布本体读起，看它把格式与尺寸的事交给编译期，然后跟着像素的调用链穿过画笔与脏矩形（记录要重画的屏幕区块），走到 /dev/fb0 的座位上，看内核怎么把显存交到用户进程的手里。最后的取舍一节只评有出处的事，出处都在源码的注释里。旧内核 GUI 那套 Window 与 WM 也不在了，`kernel/gui` 底下那四页讲的还是迁移前的旧世界，它们的重写已经排期。桌面树如今是 vesper 的 Desktop、WindowFrame、Terminal 这些控件，画布是它们共同的纸。

## 画布本体：格式表上的模板

Canvas 的定义在 `third_party/vesper/third_party/visor/include/visor/canvas/canvas.hpp`，它是一个长在编译期参数上的模板：尺寸与格式都是编译期的参数，格式还留了一个默认值 Mono1，所以早年按 `Canvas<Size>` 写好的调用点拿到的一直是单色画布，只有想要颜色的场合才把格式写明。它的存储就一个成员，`bits_` 是 `kStrideBytes * kHeight` 那么大的字节数组，行宽由格式表的 `bytes_per_row` 算出，整块缓冲的字节数 `kBufferBytes` 我们后面讲剖面的时候还要用到。读写的边界纪律也在这里定好：`set` 对越界的坐标答 false 并且什么都不碰，`get` 把越界读成格式的零值，头文件的注释说得更直白，下面的笔画不再查第二次边界，画笔的立场是笔画算法绝不在每一步查界，所以这里就是最后一道会查的关卡。

```cpp
// third_party/vesper/third_party/visor/include/visor/canvas/canvas.hpp（节选）
[[nodiscard]] constexpr bool set(base::PointI p, Pixel value) noexcept {
    if (p.x < 0 || p.y < 0) {
        return false;
    }
    const auto x = static_cast<std::size_t>(p.x);
    const auto y = static_cast<std::size_t>(p.y);
    if (x >= kWidth || y >= kHeight) {
        return false;
    }
    Format::store(bits_.data() + (y * kStrideBytes), x, value);
    return true;
}

private:
std::array<std::uint8_t, kStrideBytes * kHeight> bits_{};
```

画布对格式的态度是不指名。每张格式表都是同一个形状的结构体，里面装着像素类型、行宽函数、一张从 Ink 到像素值的映射，外加 `store` 与 `load` 的一对像素动词。Ink 是主题层的抽象颜色，落进画布才折成具体的像素值。画布把字节的加法与像素的动词全数交给表格办理，三张现成的表我们都挨个认一遍，它们住在 `pixel_traits.hpp` 里：Mono1 的像素只有一位，按最高位在前的次序拼进字节，每行的开销是 `(width+7)/8` 字节，像素的类型就是 bool，折算的规则是非黑即亮。Rgb565 的像素宽两个字节，字节按低位在前的次序摆放，绿色多留了一位，因为人眼的感知对绿色偏心。Xrgb8888 的像素宽四个字节，写进去的 alpha 一律改成不透明。单色位拼接的公式只住在 `pixel_bit_position` 一处，visor 的注释说这里是全库唯一的家，桌面那一侧的扩位也要来借。要添一张新的格式表，加一个结构体就完事了，画布与画笔的代码一行都不用动。

行的排布是画布内部的事，显示硬件的页格式它一概不冒充，注释特意交代了这句，我们交出去行字节的时候用的就是这样的行。

## 光栅剖面：一块模板的三种配置

剖面是尺寸与格式绑在一起的那对编译期参数，头文件的注释给它起的名字是光栅剖面。整幅帧、横向条带与 SwapCanvas 是同一个格式表上的三种配置。我们挑剖面的时候看的数是 `kBufferBytes`，一个构建按缓冲的预算照它采购，屏幕的真实尺寸留给运行期，管的是这一轮重画的条数、尾巴怎么裁。fbdev 桌面选中的就是条带：`fbdev_desktop.cpp` 把 `kRaster` 定成宽 4096、高 16 的 XRGB8888 画布，256 KiB 的缓冲伺候宽到 4096 像素的屏幕，入口把宽度不在 640 到 4096 之间的帧拒掉，高度不足 480 的同样拒掉。

```cpp
// third_party/vesper/examples/fbdev_desktop.cpp
// Raster storage is a strip; desktop geometry comes from the framebuffer.
constexpr SizeI kRaster{.w = 4096, .h = 16};
using Frame = visor::canvas::Canvas<kRaster, visor::canvas::Xrgb8888Traits>;
```

SwapCanvas 是这个家的第三种配置，名字里的 swap 说的是两块存储换着来。画画的动词全部落在后景上，`swap()` 干的是两块存储的对调，下一轮的笔画就落进另一块，前景观着的始终是上一轮交出去的完整画面。有宿主（把 visor 嵌进去跑的那个程序）想按自己的节奏扫描输出，`front_pixels()` 就是给它看的画面。注释把这样安排的理由交代得很清楚：visor 的缓冲存的恰好是正在渲染的那个窗口，压根不存在大缓冲的半帧更新。软件 blit（把一整块像素从一份缓冲拷进另一份的动作）的活，双缓冲就一点也不欠了。这套交换语义我们在 visor 的渲染测试（`render_tests.cpp`）里见过，交叉检查的文件（`cross_probe.cpp`）只做编译期实例化而不参与运行。fbdev 桌面走的则是条带加脏矩形的经济路线，两样都是同一块模板穿出来的。

## 调用链：从画笔到提交

控件作画时看到的脸是 `Painter` 这个纯虚接口，落到具体画布上的活归 `CanvasPainter<CanvasT>` 干，这一截接缝我们读得仔细一点。它构造的时候把主题里的七个颜色角色一次性折算成目标画布的像素，之后的每次填充只付一次查表的代价，绝不做逐像素的换算。从 Ink 到像素的那一步走的是画布自己的格式表，所以同一套动词落到单色画布上折成的就是亮与灭，彩色画布则留住主题的灰阶。画笔随身带的还有一个场景窗口，说的是缓冲不必等于屏幕：落到窗口外的笔画裁掉，窗口里的笔画按窗口的原点平移进缓冲，条带重放靠的就是这一手。

每一次落笔的活交给一支临时造出来的 `Pen`，它手里攥着的只有一个像素值，我们写笔画的时候拿它当一次性的工具就好。`plot` 落点的时候故意不理会 `set` 的返回值，注释把理由写在了行间：笔画算法的立场是不在每一步查边界，越界的落点无声地消失就好。`fill` 走的是可见范围内的整段填充，画布之外的部分它碰都不碰。

主循环 `run_dirty` 的节奏是固定的，我们一个环节一个环节地看。事件在它的手里泵干，一轮放行的键盘事件至多一个，剩下的排在后面的轮次里，指针事件倒是随到随清，注释说这样按下与松开各自占一次完整的重画。这个节奏是脏区驱动的，`run_dirty` 只画被标过的条带，跟时钟与宿主的手感无关。被标的区块记在 `DirtyRegions` 手里，容量定的是 8，重叠进来的矩形当场合并，装不下的时候把攒下的全部折成一个包围盒接着记。矩形合并成包围盒之后的那一次提交盖住的就是全部而不漏画，注释说覆盖的诚实从来不丢。`render_window` 在每条条带的窗口下把整棵控件树重放一遍，这样的重放是合法的，因为画只读状态而什么都不碰，同一棵树在每个窗口下落下的像素都一样。收尾的 `dirty.clear()` 把欠的像素清了，下一轮开始的时候没有谁还欠着。

提交的一步是 `HostTable` 的事，我们把四个指针的分工认一遍。它是核心与这个世界之间的唯一边界，四个函数指针全由宿主的接线填：flush 送的是像素，poll_event 送的是事件，clock_ms 送的是毫秒，spawn 管的是生程序。`render_window` 画完的时候调 `submit_region`，把窗口、`pixels()` 的行指针、行距与格式一起递过去，格式由 `kWireFormat<Traits>` 按画布的格式表翻好。桌面这一侧接住它的是 `fbdev_flush`，光标是它补上去的最后一层，然后整段的行字节 `memcpy` 进 mmap 到手的显存里。fbdev 桌面的表只填了 flush、poll_event 与 clock_ms，terminal 的 shell 由例子自己用 `PtySession` 拉，spawn 的座位空着也是合法的姿态，没接线的座位答的是不干活。

```cpp
// third_party/vesper/examples/fbdev_desktop.cpp
void fbdev_flush(void* context, const visor::host::FlushRegion& region) noexcept {
    auto* const session = static_cast<FbdevSession*>(context);
    const visor::ui::CanvasPainter<Frame> painter{*session->frame, region.rect, desk_theme};
    session->cursor.draw(painter);
    session->fb.blit(region.rect, region.pixels, region.stride_bytes);
}
```

## 内核侧：/dev/fb0 的座位

内核这一侧的故事从落座开始。`kernel/fs/file_world.cpp` 的 `FileWorld::init` 在文件世界立起来之后调 `RegisterFramebufferDevice`，它查一遍帧缓冲是不是活的，然后把物理帧的页范围在硬件页的登记里占住，这段页从此不会再流回 PMM（物理内存管理器、管物理页的分配和归还）的分配器，末了往设备模型里坐进名叫 fb0 的字符设备，主从号定的是 `MakeDeviceId(29, 0)`，挂的是虚拟总线，驱动名写的是 framebuffer。设备的操作表我们整段读：

```cpp
// kernel/fs/devfs/fb_node.cpp
const DeviceOps kFbOps{
    .read      = fb_read,
    .write     = fb_write,
    .has_input = nullptr,
    .ioctl     = fb_ioctl,
    .map_frame = fb_map_frame,
    .release   = fb_release,
    .context   = nullptr,
    .retain    = fb_retain,
};
```

注释说帧里字节的进出归映射管，read 与 write 的字节流在这里都不支持，read 的回答永远是 0，我们拿到的就是文件尾。映射是唯一的笔。ioctl 认得的问句只有 0x4600 与 0x4602 两个，头文件的注释注明它们就是 Linux 的 FBIOGET_VSCREENINFO 与 FBIOGET_FSCREENINFO，答话的时候把引导报告里的几何与掩码抄进两张线格式记录（回答两张查询用的结构体），身份串写的是 CinuxFB，拷进记录的身份最多十五个字节，注释还补了这句，说给结尾的空字节留了位。问句表之外的字一律答 -ENOTTY。

两张线格式记录的定义在 `kernel/driver/framebuffer/framebuffer_config.hpp`，注释的立场我们读一遍。问话的一方，是拿着 libc 的 linux/fb.h 编出来的用户程序，ABI 本身就是双方共同守着的声明。两张记录的每个字段都按 Linux 的 x86-64 形状摆，static_assert 把 160 与 80 字节的总大小、`smem_start` 与 `line_length` 的偏移一并看了起来，哪天形状漂了，断掉的就是构建，运行期不会有任何的意外。

映射的入口是 `fb_map_frame`，我们把它的次序走一遍：它查一遍帧缓冲是不是活着，再把偏移跟按页对齐的帧长比一比，越界的偏移答 `kInvalidArgument`，合法就返回物理基址按页向下对齐后加上偏移的地址，显存从这一页起交到进程的手里。

控制台与桌面共享的是同一块显存，控制台这边给帧缓冲让路：计数非零的时候 `put_char` 直接返回，控制台的笔画全停。让路的安排是一份租约，我们把两头看一遍：打开帧的那一下 `fb_retain` 把持有计数加一，画面的像素安静下来，串口的那一半照常说话。最后一个描述符关闭的时候 `fb_release` 把计数减回来，减过了零就成了租约下溢，Check（这个内核的运行时断言，命中了就停机报告）当场拦下的就是它，屏幕的光标站在原地，控制台从它停着的地方接着画。

我们也把帧缓冲设备自己的本事看一眼，因为它从头到尾只懂文字控制台的语法。init 拿引导报告的几何做体检，bpp 报上来的值不是 32 位、宽或高为零，或者 pitch 短于宽度乘四的帧一律答假，通过了就 ioremap 显存，然后按通道掩码把墨色与纸色各拼成一个 32 位的像素字。它会的动作其实只有三个：put_pixel 落的像素只有亮与灭两种，clear 刷的永远是纸色，scroll_up 搬的是整行的像素。窗口、blit 与任何二维的接口都没有，内核的代码从头到尾不画桌面，画桌面的地方在用户态。

vesper 那一侧的 `FbdevDevice::open` 拿着节点走完全套，我们跟着它进门：它开了描述符，两份几何的问话挨个来一遍，bpp 不是 32 的时候它就关掉返回假，两份都齐了才 mmap 一整段 smem_len 的显存。`blit` 把一段 XRGB8888 的区域按行 `memcpy` 进显存，双方的行距各认各的，出界的部分悄悄裁掉。`expand` 走的是单色的路，画布位里的 0 与 1 经 visor 的 cinux 绑定（visor 对接 Cinux 内核侧的适配层）扩成调用者选定的两个颜色字，单色位拼接的公式两边读的是同一处。今天的桌面是彩色的，主题的暮金与暖纸全靠 blit 一个字一个字地对上。名字的层次我们分一下：暮金与暖纸说的是桌面主题的配色，墨色与纸色说的是内核控制台的白字黑底。单色的扩位留给 1bpp 的画布，两条路共用的是同一个帧。

这一套怎么跑起来的事我们也交代一句。把 fbdev_desktop 静态链接成单个可执行的活归 `scripts/build-vesper-desk.sh`，构建的时候指明 `VESPER_BACKEND=fbdev`。`scripts/create_ext2_disk.sh` 把产物装进镜像的 /bin/vesper，拉起走的是 inittab 里 `::once:/bin/sh /etc/init.d/desktop` 的一行，overlay 的 init.d/desktop 脚本在 CINUX_SESSION 等于 gui 的时候 su 到 cinux 的名下把同一个二进制拉起来，命令行写的是 `/bin/vesper /dev/fb0 /dev/event0 /dev/event1`。event0 与 event1 是 `kernel/fs/devfs/input_node.cpp` 落座的两条输入流，从号 64 的 event0 坐的是键盘，从号 65 的 event1 坐的是指点设备，挑的依据在 `kernel/fs/file_world.cpp` 的接线里：event1 的座位只在数位板与 PS/2 鼠标之间挑，数位板在场的时候就是它，不在场的就是 PS/2 鼠标。USB 键盘走的是自己的取键路，事件流的座位上没有它。

## 设计取舍

多数安排的理由，正文都已经就地讲过，注释的出处跟着落在小节里，我们把它们排成一张指路的清单：格式走表格的出处看画布本体，尺寸走编译期与不拿整屏双缓冲的出处看光栅剖面，边界只查一次的出处看调用链，内核只守座位、控制台让路的姿态看内核侧的小节。

真正的增量只剩显示格式的边界，我们照实写在这里。内核的 init 只收 32bpp 的几何，vesper 的 open 也只认 32bpp，`fbdev_device.hpp` 的注释把这个姿态讲明：扩位的路今天只有 XRGB8888 一条，恰好就是 QEMU 的 vga std 发给裸客户机的一种，其余的排布在 open 那一步就被拒掉，`ready` 从此答的是假。16bpp 的 RGB565 通道，还在等 visor 上游的颜色核心补齐，这件事挂在桌面侧的上游清单里。

## 源码位置

- `third_party/vesper/third_party/visor/include/visor/canvas/canvas.hpp` — Canvas 与 SwapCanvas 模板本体
- `third_party/vesper/third_party/visor/include/visor/canvas/pixel_traits.hpp` — Mono1、Rgb565、Xrgb8888 三张格式表与位拼接公式
- `third_party/vesper/third_party/visor/include/visor/ui/canvas_painter.hpp` — CanvasPainter：窗口裁剪、角色表解析、每笔画一支 Pen
- `third_party/vesper/third_party/visor/include/visor/draw/pen.hpp` — Pen 的落点与填充动词
- `third_party/vesper/third_party/visor/include/visor/ui/run_loop.hpp`、`third_party/vesper/third_party/visor/include/visor/ui/dirty_regions.hpp` — render_window、render_strips、run_dirty 与脏矩形收集
- `third_party/vesper/third_party/visor/include/visor/bindings/host.hpp` — HostTable 与 submit_region
- `third_party/vesper/third_party/visor/include/visor/bindings/cinux/frame_expand.hpp` — 单色到 XRGB8888 的扩位
- `third_party/vesper/src/host/fbdev_device.hpp/cpp` — 打开、问几何、mmap 与 blit/expand
- `third_party/vesper/examples/fbdev_desktop.cpp` — 条带剖面、脏矩形主循环、fbdev_flush
- `kernel/driver/framebuffer/framebuffer.hpp/cpp`、`kernel/driver/framebuffer/framebuffer_config.hpp` — 帧缓冲设备与 fbdev 线格式断言
- `kernel/fs/devfs/fb_node.cpp` — /dev/fb0 座位
- `kernel/console/screen.hpp/cpp` — 文本控制台的让位与收回
- `kernel/fs/file_world.cpp` — 设备落座的调用处
- `kernel/fs/devfs/input_node.cpp` — event0 与 event1 两条输入流
- `scripts/build-vesper-desk.sh`、`scripts/create_ext2_disk.sh` — 静态桌面的构建与 /bin/vesper 落座
- `user/rootfs/buildroot/overlay/etc/init.d/desktop` — gui 会话的拉起脚本
