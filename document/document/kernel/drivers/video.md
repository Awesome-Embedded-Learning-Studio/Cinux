# 视频子系统

## 功能概述

这一页咱们把屏幕的来路完整走一遍。像素的旅程从引导世界开始，stage2 替咱们切好了 VBE（VESA BIOS Extension，显卡 BIOS 的模式设置接口）的模式，把挑中的几何装进交接记录交给内核。内核接住它的有三样东西，帧缓冲器件把显存握在自己的手里，文本控制台把字符变作落上屏幕的字形，devfs（设备文件系统）里的 fb0 座位，把同一块显存交到用户进程的手里。整条链只认 32bpp 的帧。用户态画布怎么用这块显存，[Canvas 画布](canvas.md)那一页已经写过了，本页聚焦的是内核侧，口径跟它对齐而不重复内容。

咱们跟着像素的路线走，从引导世界的交接记录出发（探询怎么走的，[引导链](../../boot.md)那一页讲过了），看几何跨过世界开关（源码注释里的 world switch，stage2 把机器交给内核的那一下）的走法，再看内核给显存搭的两级映射窗口，然后进 `kernel/driver/framebuffer` 认帧缓冲器件的本体，进 `kernel/console` 认字体与控制台的本体，接着看打印链的次序，末了回到 fb0 的座位与用户态的分工上。

## 引导世界：一块 32 位的线性帧

模式是怎么探出来的、为什么选这几个数（宽 1024、高 768、位深要 32，24 位的备选），[引导链](../../boot.md)那一页已经走过了，咱们不重走，这一页接的是探询的落点。中选的模式号交给了 SetVideoMode，屏幕切进了 32 位的线性帧，describe_framebuffer 与 fill_boot_info 把物理基址与行距、宽高、位深连同红绿蓝三路掩码的位置誊进交接记录，行距（pitch）说的是一行像素占的字节数，内核后面拼像素字用的就是掩码的这几位数。

引导世界自己的嘴只有串口一张，屏幕上不会出现引导的文字，头一行字要等咱们把控制台点亮了才出现。

## 交接记录：几何跨过世界开关

帧记录的形状定在 `kernel/boot/boot_info.hpp`，咱们把 FramebufferInfo 的定义读一遍：

```cpp
// kernel/boot/boot_info.hpp
struct [[gnu::packed]] FramebufferInfo {
    uint64_t physical;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
};
```

packed 是刻意的，文件头的注释说明了缘由，引导世界用 32 位的形状写记录，内核用 64 位的形状读它，两边的填充规则不一样，索性把填充禁了。test_image.cpp 用断言把 48 字节的大小与记录在整份交接记录 BootInfo 里的 2576 偏移看了起来，咱们把数字记在这里，哪天字段动过而版本号没跟上，断的就是测试。

交接的路径咱们也跟一遍。记录的本体 g_boot_info 住在 stage2 的数据里，stage2 把它的地址写进交接信箱（kHandoffMailboxInfo 这个字），世界开关的落点从信箱取出地址递给内核，读取的动作发生在引导侧的长模式入口 boot/lm/lm.cpp 里，地址进了内核入口之后，BringUpAddressSpace 再把它归档进了 HandoffArchive，此后内核用的都是自己的存档。

## 显存的两级映射窗口

第一级窗口是引导世界搭的。咱们在 boot/lm/page_tables.cpp 里读 BuildHandoffDoors，它调的是 PlanDeviceDoors，算出盖住物理基址到物理基址加行距乘高这一跨度需要几条 1 GiB 的大页条目，结尾跨过 1 GiB 边界的时候向外取整，条目写进引导页表的低位身份区，也就是虚拟地址就等于物理地址的那一段页表。test_page.cpp 给这套规划留了断言，0xFD000000 起的 3 MiB 落在第 3 号窗口、一条就够，跨界的落两条，零尺寸答的是零条。

第二级窗口是内核自己搭的。MapFramebufferDoor 在地址空间这一步（引导序列的第三行）里跑，把同样的跨度接进了 ioremap 区，占的是 PML4 的第 259 号槽位。kIoremapBase 的值定在 0xFFFF818000000000，IoremapVirt 把物理地址原样加上了这个基址，layout.hpp 的注释说第一阶段照搬物理偏移，为的是帧缓冲这样的早期消费者不必等一个分配器。窗口里的每条 2 MiB 大页都带着可写位，缓存禁用位倒是没有带，咱们把同一个文件里的 MapDeviceWindow 拉来对照，它给别的设备窗口都加了 kCacheDisable，读寄存器的设备要的是不缓存的访问，显存的窗口没有把它带上。vmm.hpp 给 MapFramebufferDoor 的警告是它必须赶在任何代码经由这个别名碰显存之前跑完，注释还说了这一块可以单独被测试内核借用，要的是屏幕而不是整套接管。

framebuffer.hpp 里有一处对不上的地方，咱们照实记下。它的文件头还写着写显存走的是引导页表给的身份映射，init 的实际代码取的是 IoremapVirt 的别名，test_console.cpp 回读像素走的也是 IoremapVirt 的别名，读的时候以代码为准。

## 帧缓冲器件：墨与纸的像素字

Framebuffer 的定义在 `kernel/driver/framebuffer/framebuffer.hpp`，它是 Meyers 式的单例。咱们把它的体检读成四条，bpp 要的是 32，宽与高要的是非零，pitch 短于宽度乘四的不要，有一条不过的话就答假，器件从此就哑着了。过了体检就按 IoremapVirt 把帧的物理基址折成带 volatile 的 32 位字指针，随后把墨与纸（控制台的前景白与底黑两个颜色字）各自折算了一次存进成员：

```cpp
// kernel/driver/framebuffer/framebuffer.cpp
constexpr uint32_t compose_pixel(const cinux::boot::FramebufferInfo& layout, Paint paint) {
    uint32_t const kRed   = static_cast<uint32_t>(paint.red) >> (8U - layout.red_size);
    uint32_t const kGreen = static_cast<uint32_t>(paint.green) >> (8U - layout.green_size);
    uint32_t const kBlue  = static_cast<uint32_t>(paint.blue) >> (8U - layout.blue_size);
    return (kRed << layout.red_shift) | (kGreen << layout.green_shift) |
           (kBlue << layout.blue_shift);
}
```

compose_pixel 的算术咱们一行行读，每个通道都按掩码的宽度截短，再按各自的位置挪进 32 位字里。墨与纸在头文件里是两个 constexpr 的 Paint，墨是全亮的白，纸是全黑的底色，纸字无论掩码怎么给都是全零，墨字在虚拟机 8/8/8 的掩码下拼出来是 0x00FFFFFF，最高的一个字节空着。折算只发生在 init 的时候，之后落像素的时候就不再逐次换算。

它的动作一共只有三个，咱们挨个认一遍。put_pixel 查的是边界，越界了就直接返回，字面上就是 ink_ 与 paper_ 的二选一。clear 干的是把纸字按行按字刷满整帧。scroll_up 干的是把整帧向上搬 line_count 行像素，腾出的底带刷回纸色，要搬的行数不小于屏高的时候，整个刷成纸色也就到了。动作之外还有的是一个问句，frame_bytes 答的是行距乘高，fb0 的座位拿它算映射的跨度。名字的口径跟 [Canvas 画布](canvas.md)对齐，墨与纸说的是内核控制台的白字黑底，跟桌面主题的配色不相干。

## 文本控制台：字体、光标与滚动

TextConsole 的家在 `kernel/console/screen.hpp`，它把字体、光标与帧缓冲三方的活接在一起。咱们按 init 的次序读，EmbeddedFont 解析嵌进镜像的字体，screen.init 让帧缓冲过一遍它的体检，再检查字号放不放得进屏幕的范围，三关都过了就 clear 一遍、点亮 alive_。任何一关没过的话就答假，屏幕从此就安静了，put_char 成了无操作，screen.hpp 的文件头注释交代了这样安排的道理，中性的控制台才能无条件地扇出。

字体是 PSF2（PC screen font 的第二版，位图字体的一种格式）。ParsePsf2 是 constexpr 的解析器，认 0x864AB572 的魔数与 32 字节的头，字形宽的上限是 8 像素，一行位图恰好一个字节、最高位在最左的摆法，宽了就拒。资源的来路咱们也认一下。资源的文件是 assets/font.psf，一共 4128 字节的个头，装着 256 个 8×16 的字形，构建的时候 objcopy 把它转成目标文件，段名改作了 .rodata.font，符号改作了 g_font_psf_start，收尾的改作了 g_font_psf_end，改名的出处是 kernel/CMakeLists.txt，font.cpp 拿符号夹出了整块字节再交给解析器。

光标（代码里的 caret）的步进归 console_grid.hpp，咱们跟着它的状态转移走一遍，它管的是下一个字符落在哪、格子什么时候要滚。换行落到下一行的行首，回车回到本行的行首，退格退的是一格，到了第 0 列就停住，普通字符消耗的是一格，到了右边缘就自动换行，在最后一行再换的话就原地滚动，把 scrolled 置成了真。TextConsole::put_char 看到 scrolled 置成了真，就把 screen.scroll_up(font_.height) 调了一回，滚过去的高度正是字形自己的高度。列数与行数是屏幕尺寸除以字形尺寸的商，1024 与 768 除出来的就是 128 列 48 行，kernel.cpp 起来之后打印的 screen console 一行报的就是它们。画字形的是 paint_glyph，一行一行地取位图字节，一位一位地判点亮，逐个像素地调 put_pixel，编号超出字库的字形折成 0 号。

帧的租约咱们也认一下。租约的两头、计数加减与 Check 拦下溢的立场，[Canvas 画布](canvas.md)那一页已经讲过了，咱们不重念，租约落到控制台就是 put_char 起手的一查，frame_holders_ 的计数非零，笔画就停而串口的那一半照常说话。

## 打印链：串口在前，屏幕在后

屏幕在打印链里只占一半的位置。print::Println 的格式化与缓冲、进门闸与第五步的次序，[串口驱动](serial.md)那一页已经讲过了，咱们只认两件事。console::PutString 的 put_char 给每个字符发两份，串口的一份、文字屏幕的一份。串口排在第一行而屏幕的点亮排在第五步的道理，console.hpp 给 InitConsole 的注释说了，调试不等一块就绪的屏幕。

ktest 的测试世界（test/kernel/test_main.cpp）调的是 console::InitConsole，一句话把 SerialInit 与 TextConsole::init 一起办了，生产世界分两步走的是同一个次序。屏幕活着的凭证咱们在 kernel.cpp 里也见过，bootinfo 一行打印 fb 的宽高与位深，屏幕就绪的时候再补一行 screen console 的列数与行数。

## fb0 的座位与用户态画布的分工

fb0 的座位立在文件世界起来的时候。落座的次序、硬件页登记的占住与问几何拿映射的走读，[Canvas 画布](canvas.md)那一页的内核侧一节已经走过了，咱们不重走，这一页补的是座位自己的三句。fb_node.hpp 的注释写明了缘由，身后没有像素的 GUI 线走到这一步，屏幕早就丢了，所以帧缓冲死着的话落座就 Check 当场停机。字符设备的名字坐的是 fb0，主从号定的是 `MakeDeviceId(29, 0)`。问句进出的形状也有内核侧的一段，syscall 层的问句表把两个 fb 问句登记成 160 与 80 字节的取回，应答头一回落进一个内核侧的中转缓冲，再抄回用户空间给的地址，表里最宽的问句就是 160 字节的 var 记录，中转缓冲的尺寸按最宽的问句定下来。

分工的边界在这里立着，内核侧管几何与格式的真相，把显存原样地交出去，内核的 init 只收 32 位的几何，这是内核侧硬的一条。vesper（用户态的桌面，住在 third_party/vesper 这个 submodule 里）那一侧只认 XRGB8888 的排布、16 位的 RGB565 还在上游的清单里等着，出处与桌面侧的细节都归 [Canvas 画布](canvas.md)的设计取舍一节，咱们不重复。

## 源码位置

- `boot/stage2.cpp` — 模式切换的落点与交接记录的填写
- `boot/lm/page_tables.cpp` — BuildHandoffDoors 里帧的身份窗口
- `boot/layout.hpp` — 交接信箱字 kHandoffMailboxInfo 与 world switch 的注释
- `boot/lm/lm.cpp` — 长模式入口，从信箱取出地址经 rdi 递给内核
- `kernel/boot/boot_info.hpp` — FramebufferInfo 与 BootInfo 交接记录
- `kernel/mm/layout.hpp` — kIoremapBase 与 IoremapVirt
- `kernel/mm/vmm.cpp`、`kernel/mm/vmm.hpp` — MapFramebufferDoor 搭 ioremap 别名
- `kernel/driver/framebuffer/framebuffer.hpp/cpp` — 帧缓冲器件本体
- `kernel/driver/framebuffer/framebuffer_config.hpp` — fbdev 两张线格式记录
- `kernel/fs/devfs/fb_node.hpp/cpp` — fb0 座位
- `kernel/console/screen.hpp/cpp` — TextConsole
- `kernel/console/font.hpp/cpp` — PSF2 解析与嵌入字体
- `kernel/console/console_grid.hpp` — 光标步进的语法
- `kernel/console/console.hpp/cpp` — 控制台扇出与闸
- `kernel/boot/print.hpp` — Println 与 192 字节缓冲
- `kernel/init/init_sequence.cpp` — console 步的次序
- `kernel/mm/hardware_frames.hpp` — 硬件帧登记
- `kernel/fs/file_world.cpp` — RegisterFramebufferDevice 的调用处
- `kernel/CMakeLists.txt` — 字体资源的 objcopy 接线
- `test/unit/test_vesa.cpp`、`test/unit/test_page.cpp`、`test/unit/test_image.cpp`、`test/kernel/test_console.cpp` — host 侧的形状、窗口规划与租约测试
