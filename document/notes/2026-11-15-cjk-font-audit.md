# CJK 字体路径审计：桌面与内核控制台各缺什么

状态：审计完成，未动代码（2026-11-15）。按"先审计、再提中立层"的原则，这份先
把 x86 与 armv7 共用的两条文字渲染路径查清楚，回答"CJK 为什么显示不出来、缺的
是哪一层"，下一轮再据此动手。

## 两条路径，两个瓶颈

### 桌面（vesper，第三方，GUI 车道）

文字渲染链是：UTF-8 字节流 → `visor::base::text::utf8` 解出码位 →
`MonoFont::glyph(codepoint)` 按码位二分查找 → 按位画点。

渲染架构本身不缺任何东西：

- `visor/base/text/utf8.hpp` 已有完整的 UTF-8 解码；
- `MonoFont::glyph` 按码位查找，找不到返回空指针（不是截断到低字节）；
- 字形表由 `tools/fontgen` 从 BDF 源生成，BDF 解析器支持任意码位，生成时排序，
  查找是二分；
- `fontgen` 接受 `--range first:last`，范围不是硬编码 ASCII。

CJK 缺字的唯一原因是字体源：`third_party/vesper/scripts/fonts/` 下的三个 BDF
（ui8、term12、title10）都只含 ASCII，生成头文件时写死了
`--range 0x20:0x7e`，四个生成的 hpp 各带 95 个字形。缺的是一份含汉字的 BDF
源加一次重新生成，不是一层架构。

### 内核控制台（kernel/console，双架构共用）

`TextConsole::put_char(char)` 按字节画字：字形索引就是原始字节值，上限
`assets/font.psf` 的 256 个 8x16 字形。这条路径没有 UTF-8 解码，多字节序列会
被逐字节当 Latin-1 画成乱码。

但这不是 GUI 的阻塞项：内核控制台服务的是 `print::Println` 和串口输出，桌面
（vesper 是独立进程）不经过它。串口本身也只收 ASCII。它的中立状态反而是好
的——font.cpp、screen.cpp、console.cpp 在两条架构的 CMakeLists 里是同一份
源文件，没有第二条实现。

## 结论与下一步

1. CJK 进 GUI 的路径：找一份含常用汉字的 BDF（12-16 像素、约 3000-3500 常用
   字，或按需子集），用现成的 fontgen 生成新的字体头，vesper 的会话/终端把
   字体源换过去（或做 ASCII+CJK 两级回退：`MonoFont::glyph` 返回空时落回
   ASCII 字体或画占位框）。渲染层零改动。
2. 缺字回退已查实：`TextLayout::next()`（mono_font.hpp）对字体没有字形的码位
   是静默跳过——不画墨、不进笔、不画占位框，注释里写明了这条规则。所以就算
   把 UTF-8 汉字喂给现在的桌面，也只是字消失，不会乱码或崩。要不要在接入 CJK
   字体的同时补一个"缺字画占位框"的回退，是接入时的一个设计决定：终端类画面
   （shell_desk）不回退会吞字，标题类漏字可接受。
3. 内核控制台维持字节语义不动：它不挡 GUI 的 CJK，改它反而要动双架构共用的
   打印路径，收益低。
4. 字体头的体积预算：16x16 汉字每字形 32 字节位图，3500 字约 112 KB 位图加
   字形表，嵌进内核镜像可接受，但 vesper 的字体是用户态资源，是否进内核镜像
   由 rootfs 打包决定，不进。

下一轮动手：找 BDF 源、用 fontgen 生成、决定 shell_desk 的缺字回退、接入。
