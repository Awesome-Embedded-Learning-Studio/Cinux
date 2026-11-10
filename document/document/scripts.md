# 构建脚本

## 功能概述

这一页我们把构建的链条从头走到尾。从一份源码到一张能开机的镜像，中间要经过的脚本与构建目标都住在仓库的两个地方，cmake/ 下的模块管编排，scripts/ 里的实干脚本等着被调用。我们按链条的次序讲完这一页，你打开仓库里任何一份脚本，都应该说得出它是给谁用的、产物落在了哪里。

旧页的几处事实已经过期，我们按现在的代码树一一纠正。旧页列过的 build_image.sh、debug_qemu.sh、launch_qemu_debug.sh、qemu_test_wrapper.sh、run_all_tests.sh.in、append_crc32.py、check_memory_layout.py、generate_large_elf.py，这些在今天的树里都找不到了。磁盘拼装的活儿有了新去处，如今由 add_cinux_image 接管了，它的家在 boot/CMakeLists.txt。成品是 1 MiB 的 cinux.img，MBR 占的是 0 号扇区，stage2 占 LBA 1 起的 28 个扇区，而内核从 LBA 32 进镜像。旧页写的 16 号起放 mini.bin 是退役前的老布局，本页不再保留这样的行。至于镜像上电之后是怎么跑起来的，[引导链](boot.md)那一页已经写过了，本页管的是链条之外的建设与驾驶。

有几处边界我们不在这里展开，base 库与 host 测试的分工归 [内核库](kernel/lib.md)，arch 目录拆分的细节归 [x86_64 架构层](kernel/arch.md)，调度语义在 host 侧的验证见 [进程与调度器](kernel/proc/process_and_scheduler.md)。

## 入口与门槛：根 CMakeLists.txt 与三张旗标卡

构建怎么起步，根 CMakeLists.txt 说了算，我们进去就能看到整个项目怎么站起来。它把 cmake/platform 塞进了模块搜索路径，平台的声明只有一行（include Platform/Linux），CMake 照着 Linux 的方式来认这个项目。门槛设置了两道，CMake 的下限是 3.16，GNU 家族的编译器要 GCC 14 以上，达不到的话当场就是 FATAL_ERROR，报错文本写明了缘由，C++23 freestanding 加严格警告是这个项目的底线，freestanding 就是不带操作系统与完整标准库的编译环境。语言标准定在了 C++23，扩展是关着的，CMake 的模块扫描也关了，compile_commands.json 也导了出来，供 clang-tidy 一类的工具使用。python3 找不到的话配置就停了，因为后面的 kallsyms 与镜像拼装都要用它。并发也被压了一道，编译池给了 2 个并发（CINUX_COMPILE_JOBS），链接池只给了 1 个（CINUX_LINK_JOBS）。末尾的 include 与 add_subdirectory 把次序排好了，boot、base、user、kernel 一个个地来，然后轮到了 rootfs.cmake 与 buildroot，再往后是 QEMU 的设备清单，test 目录挂了 EXCLUDE_FROM_ALL，这样就不拖累日常构建了，压轴的是 QEMU 的运行目标。

旗标的事归三张卡管，谁链接了谁就享受，我们从警告卡认起，它的文件是 warning_standard.cmake，住在 cmake/compilers/ 的下面，造了接口库 cinux_warnings，从 -Wall 到 -Werror 的五样警告是全开的。boot 卡的文件是 boot_modes.cmake，一口气造了三个接口库，按位宽取了名，16 位的叫 cinux_boot16，32 位的叫 cinux_boot32，64 位的叫 cinux_boot64。共同的部分是 -Os 加一整套裸机的旗标，其中的 freestanding 摆在头一位，PIE 与栈保护是关着的，展开表与异常、RTTI 也一并关了，SIMD 寄存器同样收掉了（-mgeneral-regs-only），64 位那一档额外加了 -mno-red-zone。内核卡的文件是 kernel_modes.cmake，接口库的名字是 cinux_kernel_flags，基座定在了 -m64，Debug 配置走的是 -O0，其余配置走的是 -O2，ffreestanding 是当然的成员，栈保护开到了 strong 并用全局的守卫字，帧指针是留着的，-ftree-vectorize 也是开着的，红区同样收掉了。各目录的 CMakeLists 只管挑卡，旗标就不在别处重复出现了。

base 的 CMakeLists 是挑卡目录里最短的一份，我们顺路进去，看到的就是头文件路径与静态库两件事。头文件的路径由 cinux_base_include_only 提供，静态库 cinux_base 装了五个源文件，format 的入口、变参与进制在内，parse 与 path 也一并编了进去。它把 cinux_host_checks 链在了 PUBLIC 一侧，所以 host 测试一链 base，检查开关的旗标就流了进来，这个安排 [内核库](kernel/lib.md) 已经讲过了，我们不重复了。

## 从源码到镜像：boot 与 kernel 的 CMake

引导件怎么造，我们头一站就到 add_cinux_boot_binary 的门口，它的家安在了 cmake/boot/boot_target.cmake。收的三个参数是目标名、位数与链接脚本，依次传了进去，链接时挂上警告卡与对应位数的 boot 卡，-nostdlib、-no-pie、自己的 .ld。收尾的 objcopy 是 binutils 家族里的工具，干的活是把 ELF 折成平铺的二进制，折出来的就是 target.bin，我们后面的流程里到处会碰见它。MBR 就是这么造出来的，用的是 16 位与 mbr.ld，外加 --gc-sections 把没人引用的段剪掉了。

MBR 是头一层的活，stage2 的组成要多几件。boot_pm 是一个 OBJECT 类型的库，装着保护模式进出与 32 位搬运的几个源文件，kernel/driver 的 io.cpp 被它收编了，serial.cpp 也一并进来了，引导世界要用串口的时候，就借了内核驱动的源码一程。lm_blob 是一段单独链接的 64 位代码，用的是自己的 lm.ld 与 -mcmodel=small，objcopy 把它转成 elf32-i386 的目标文件，段名改成了 .lm_blob，16 位的 stage2 把它当数据背在身上，跳进去的那一刻机器就进了长模式。stage2_size_check 是一个 ALL 类型的目标，它用正则从 boot/layout.hpp 里抠出了 .sectors 的数值，我们今天看到的值是 28，stage2.bin 一旦超过 28 个扇区的话就 FATAL_ERROR，报错文本指了路，去抬高 kStage2Spot 的扇区数，MBR 的 DAP（磁盘地址包，INT 0x13 扩展读用的参数包）会跟着走。改 layout.hpp 的话配置会自己重来，CMAKE_CONFIGURE_DEPENDS 也把它看住了。

stage2 说完了，镜像里剩下的两个座位归 add_cinux_image 填，我们跟着四条 dd 命令走一遍。它造一张 1 MiB 的空盘，mbr.bin 写到了 0 号扇区，stage2.bin 落在了 LBA 1 起的位置，kernel.bin 落在了 LBA 32 上，对上的正是 layout.hpp 里定下的数。常数的名字是 kKernelImageLba，家安在了 layout.hpp 里。主线产物写在了 build/boot/cinux.img 里，目标的名字是 boot_image，再往上的 cinux_image 挂了 ALL，日常构建顺手就有了。每个机内测试内核也各有一张拼出来的盘，目标的名字成了 boot_ktest_名字.img，内核换成了 cinux_ktest_名字.bin。至于座位背后的道理，[引导链](boot.md)在磁盘布局一节讲过了。

盘拼好了，内核本体是另一摊的事。kernel 的 CMakeLists.txt 把整棵树 GLOB 了进来，配置带了 CONFIGURE_DEPENDS，新增的文件不用重配，分堆的法子是按路径来，/arch/${CINUX_ARCHITECTURE}/ 的归到了架构侧，其余的归 portable 侧，两边各自排了序，链接顺序就稳定了。CINUX_ARCHITECTURE 是缓存型的变量，今天认的是 x86_64。特例也是有的，kernel/net/tcp.cpp 有专属的旗标，-Wframe-larger-than=4096 是它独享的，源码注释交代了缘由，我们读注释就知道，包处理在 16 KiB 的线程栈上嵌套，连接控制块的临时对象若在栈上冒出来会破坏调用者的栈，编译期就把这样的意外拒掉了。还有一份写死的名单，名单的名字是 CINUX_KERNEL_NO_SSE_ISLAND，三十个文件被单独加上了 -mgeneral-regs-only，调度器、中断入口与核心驱动的一些文件都在内，谁要进出名单就得改这里的 CMakeLists。

分堆说完了，内核的身体里还编着两样资源，来路我们也认一下。字体是 assets/font.psf 给的，objcopy 把它变成了目标文件，段名改成了 .rodata.font，符号也改了名，一头的符号是 g_font_psf_start，另一头的符号是 g_font_psf_end。initrd 是 assets/initrd 的内容，tar 打成了 ustar 格式再走同一套 objcopy，符号落在了 g_initrd_start 与 g_initrd_end。

资源备齐了，add_cinux_kernel 是造内核的入口，收的参数是名字与 bin_stem，bin_stem 定的就是内核二进制的文件名，kernel.bin 叫什么由它说了算，我们把它的流水线走一遍。它拿全部 portable 与架构的源文件，再加上 base 的格式与路径文件、boot 的早期 libc，编出了一个 OBJECT 库当内核体，可执行体是 kernel.cpp 加上这个库与两个资源 blob 的组合，宏 ENTRY_SYMBOL 定成了 KernelEntry，链接脚本用的是 kernel.ld，入口定在了 -e KernelEntry，--gc-sections 也挂上了。这个可执行体的名字是 unsym，此刻它还没有自己的符号表。scripts/gen_kallsyms.py 接着上场了，nm -n --defined-only 扫了一遍 unsym，留下的是 T/t 类型，跳过 .label 这类链接器生成的标记，还验了地址严格递增（脚本注释说查表靠的是二分，顺序错了就直接退出），写出一个定义 kBuiltInKallsyms 的 C++ 文件，最终的内核再链它一次，符号名表就这样长在了内核的身上。脚本注释还交代了一个体贴的细节，生成的文件只在内容真变了才重写，镜像没变的话源文件就不重戳了，也就不会每次都触发最后的链接。

最终的 ELF 由 objcopy 转成了 kernel/kernel.bin，转完就过尺寸验收的关，把守的是 cmake/kernel/ 下的 `image_size_check.cmake`。镜像不足 40 字节的话直接拒，偏移 16 起的 8 个字节按小端读出声明的文件大小，跟真实大小对不上的话也拒。那个数不是随便写的，链接脚本的名字是 kernel.ld，镜像头由 image_header_data.cpp 折了出来，声明的数就写在头里。stage2 验收镜像用的是同一份头（[引导链](boot.md)的交接记录一节写过）。模板是 cmake/debug/ 下的 kernel.gdb.in，configure 出来的产物是 build/kernel.gdb，我们数了数只有五行，认的是 build/kernel/ 下的 cinux_kernel，架构定成了 i386:x86-64，连的是 :1234 端口，在 KernelEntry 上设了硬件断点，最后的一行是 continue，run_debug 起的 QEMU 等的就是它。

## QEMU 怎么跑：设备清单与三个 run 目标

QEMU 上场时认的设备，清单写在了 cmake/qemu/devices.cmake 里，我们对的就是它。QEMU 程序默认用的是 qemu-system-x86_64，内存默认给了 1024 MiB，加速用的是 KVM，CPU 的型号按 host 直通，核给了两颗，显示默认是不亮的，自动重启被禁止了。isa-debug-exit 也挂上了（0xf4 端口），往那里写一个字节就退场了，机内测试报成绩用的就是这个字节，交到端口的字节是失败数加一，QEMU 进程的退出码是它的两倍加一。AHCI 控制器挂了一张 ext2 盘，xHCI 挂的是 USB 键盘与 tablet，用户态网络挂的是 e1000 网卡。CINUX_QEMU_NVME 默认是开的，scratch 盘由 create_disk.py 造了出来，脚本放在 cmake/qemu/ 的下面，用的是独占创建模式，文件已存在的话原样保留，盘挂上了 NVMe，串号定的是 cinux-nvme，qemu_disks 目标管的就是这批盘。

设备清完了，Linux 上还有一个默认就开的开关，名字落在了 CINUX_QEMU_SLIRP_WAKEUP 上。cmake/qemu/slirp_wakeup.c 被编成了动态库，宿主侧用的，C11 写的，编译的旗标是 -Werror。QEMU 的启动命令也换了人，打头的是 python3 与 qemu_launch.py，脚本的家在 scripts/mcp/ 里（MCP 指 Model Context Protocol，模型调用外部工具走的一套协议，这个目录住的就是它的服务端脚本），后者做的是 LD_PRELOAD 加 exec 原 QEMU。库头上的注释交代了它修什么，libslirp 在宿主侧新建了 socket 之后，QEMU 主循环的 poll 集合要重建才能看见新的描述符，包进来了却没人醒来，它干的事是在 slirp_input 之后 notify 主循环一把。启动器保住了 QEMU 的 PID、退出码、信号与 gdbstub 生命周期，你拿到的 QEMU 还是原来那个 QEMU。QEMU 版本太老不带符号的话，库会把退路报了出来，配置期给的是 `-DCINUX_QEMU_SLIRP_WAKEUP=OFF`，运行期的话把它关掉也行，设 `CINUX_QEMU_SLIRP_WAKEUP=0` 就行了。

这套参数的用处不止 CMake 一家，我们自己写脚本起 QEMU 的话也吃现成的。qemu-command.txt 存的是命令，qemu-device-args.txt 存的是设备参数，run 的在 qemu-run-args.txt，GUI 的在 qemu-gui-args.txt，每个文件的格式是一行一个参数，落在了构建目录里，scripts/mcp/qemu_args.py 读的就是它们，所以脚本起的 QEMU 与 CMake 起的 QEMU 看到的是同一套参数，两条路就不会漂了。

参数备齐了，cmake/qemu/run.cmake 用它们造了三个目标。run 是日常的驾驶档，参数里的生态夹具盘会被换成日常用的开发卷，默认的卷是 build/boot/native-build.ext2，Buildroot 烤出来的 1536 MiB，盘里带着整份的源码，烤它的是 buildroot_volume 目标，串口接的是 stdio。run_debug 多了 -S -s 两个参数，机器冻在了复位处等调试器，屏幕会提示你用 gdb -q -x build/kernel.gdb 挂上来。run_gui 是最讲究的，脚本里有一段成对的循环把 -device 与 -display 逐对摘出来重排，源码注释解释了为什么不用按值删除，按值删会删掉头一个碰见的 -device 字样，参数串就被孤立掉了。USB 键盘被摘掉了，键盘改走 PS/2 了，tablet 是留着的，显示换成了 GTK 并开了 zoom-to-fit，再由 fw_cfg 给内核递上初始的环境，值定的是 CINUX_SESSION=gui。三个目标起手都要过 cmake/qemu/check_rootfs.cmake 的关，默认卷缺失的话会自动烤出来，-DCINUX_QEMU_ROOTFS 指来的自定义路径，不存在的话就直接 FATAL_ERROR。

## 磁盘卷怎么烤

磁盘侧的总装文件是 cmake/rootfs.cmake，生态卷的活归 ext2_img 目标管，管的是 build/boot/ext2.img，它把活交给了 scripts/create_ext2_disk.sh。喂过去的是环境变量，我们数一数名单，十几个用户态的 ELF 在列，从 user_hello 一直排到 user_smpstress 的位置，配角的名单有四样，busybox-glibc 与 hello-glibc 是其中的两样，toolchain-staging 算的是第三样，vesper 的桌面补上画面，种子文件变了也会重烤，fstab 与 inittab 是其中的例子，其余的 passwd、group、smp-stress.sh 也算。ext2_ktest_img 复制了一份，给机内的测试当 scratch，怎么写都不心疼共享的夹具盘。

create_ext2_disk.sh 认的是两条路，默认的是 glibc 路，我们把搭树的过程看一遍。build/rootfs-assemble 被它当目录树搭了起来，/etc 放的是种子文件，/bin 放的是 busybox，二十多个 applet 的名字硬链到了同一个 inode 上，cat、ls、ps 也是名单上的，一直排到了 sh 与 mount，sbin/init 的座位也在，十二个教学 ELF 各就各位了，glibc 的运行库用 gcc -print-file-name 就地取材，vesper 桌面在场时往 inittab 里补了一行 respawn，工具链 staging 树整个叠在了最上层。mkfs.ext2 用 -d 把目录树一把灌进了镜像，规格是 192 MiB 的盘，1 KiB 的块，特性是全关的，inode 给到了 16384。走目录树而不走逐文件的 debugfs，这是脚本注释解释过的，mkfs.ext2 -d 保住了硬链接，busybox 一个 inode 挂所有的 applet 名字，SONAME 的符号链接也保住了，这样的规模 debugfs 表达不了，192 MiB 的规格跟着自编译卷的量级走。

CINUX_LIBC=musl 的话是另一条 16 MiB 小盘路，inode 的数目是 512，逐个地用 debugfs 写进去，它的逻辑我们也顺一遍。动态链接器在 ~/.local/musl 与系统的 musl 路径里找，找得到的话就装进 /lib，找不到的话只留一行提示，盘照常烤出来了。静态的 busybox 由 build-busybox.sh 编出来，源码树是现成的，落在了 ~/.local/src/busybox-1.38.0 里。

busybox 的静态版编完了，动态版就费事多了，build-busybox-glibc.sh 管的就是这一摊。busybox 1.38.0 的压缩包带 SHA-256 校验，压缩包落在了 .cache/rootfs，缓存的位置由 CINUX_ROOTFS_CACHE 改，配置里关掉了静态，加上了 -fno-pie，动态链接器的路径指向 /lib64/ld-linux-x86-64.so.2，编出一份带 PT_INTERP 的动态 busybox。它还算了一份输入指纹，配置、脚本、源码树的逐文件哈希、libc 与编译器版本都在内，指纹没变的话就直接复用上次的产物，重跑构建就不用重编 1.38.0 了，我们改一行配置的话指纹立刻跟着变，该重编的还是会重编。

自编译卷的粮草队是 build-toolchain-staging.sh，搬的东西我们也点一遍。宿主的 GCC/G++ 闭包搬了家，新家安在了 build/toolchain-staging，用的是绝对路径。as、ld 跟动态链接器都来了，cc1、cc1plus、collect2 是编译器的内部程序，libstdc++、libm、libatomic 也搬了，SONAME 的符号链接一并补上，整套的 crt 对象与 libgcc 也在。头文件的清单靠 -H 收集，hello.c 与 hello.cpp 各跑了一遍，stdc-predef.h 也带上了。它把客人（guest 就是将来要跑在 QEMU 虚拟机里的那一侧，本文后面都这么叫）将来要重放的链接命令一行行写进了 hello.ldargs，从 Scrt1.o 到 crtn.o 的次序原样落纸，客人照着重放就能编出默认 PIE 的程序，冒烟的产物备了三样，hello.s 是其中的一个，execprobe 与 sendfileprobe 是另外的两个。指纹缓存在输出目录旁的 .meta 里，gcc 与 g++ 的版本或目标三元组对不上就当场拒绝了。链接脚本写的是宿主的多架构路径，树里要把这些路径连同客人的扁平搜索目录一起保住。

客人那侧的桌面有专门的一段，build-vesper-desk.sh 干的就是从源码把它烤出来。源码是 third_party/vesper 的 git 子模块，缺了它的话脚本会大声报错，报错信息给了补法的命令（`git submodule update --init --recursive`），我们照着敲就行。构建做在自己的 out-of-tree 目录里，用的是 Ninja，选的是 Release，链接是全静态的，后端定的是 fbdev。产物在拷走之前还要过 file 的一眼，不是 statically linked 的话就拒收。脚本注释交代了这么绕一圈的道理，客人拿到的桌面二进制必须来自本仓库自己的构建，而不是桌面仓库构建目录里上一轮的遗留物。

动态链接的验收由两份 hello 承担，参数的用意我们也对一对。glibc 的版本住在 scripts/glibc/ 里，musl 的版本住在 scripts/musl/ 里，两边都编非 PIE 的 ET_EXEC，两边都把客人那侧的解释器路径用 `-Wl,-dynamic-linker` 烤进了程序，脚本注释说清了原因，宿主工具链自带的路径在客人的盘上是不存在的。

最重的目标叫 kernel_selfhost_volume，卷里装了什么，我们把导出的内容清点一遍。导出的活由 prepare-kernel-selfhost.py 干，它的家在 scripts/ 下，把当前配置的内核构建导成了一个纯源码卷，内核与 base 的源码、assets、生成的配置头都进了卷，编好的 mbr 与 stage2（当自举输入）也在，python3 运行时连同它 ldd 出来的库，编译与链接命令整理进了 build.json 随卷走，卷里不带任何内核的对象文件。客人的那边由 build-kernel-in-guest.py 接手，从空的输出目录把所有的编译单元跑一遍，在机内生成了资源对象与 kallsyms，链接跑了满满两轮，镜像头也核验过了，启动盘也拼出来了。scripts/checks/README.md 记着它的规格，验收的脚本就是 `scripts/test_station34_kernel_selfhost.py`（station 是这片仓库给验收步骤编的站台号，脚本名里 test_stationNN 的前缀就是站牌，33 号站管的是生态扫描，34 号站管的是自编译，35 号站管的是 GUI，产物与日志也落在了 build/stationNN/ 下），256 MiB 的源码卷，512 MiB 的客人内存。产物的住处分在两处，盘的一份在 build/boot/ 下，名成了 cinux-selfhost.img，内核的安家处是 build/kernel/，名成了 cinux_kernel_selfhost。

## 测试的两层：host 套件与机内 ktest

host 测试这层的家是 test/CMakeLists.txt，登记的活由 add_cinux_test 一个函数管。每个套件是 unit/test_名字.cpp 加上框架的四个固定件（`framework`、`assert_host`、`per_cpu_host`、`irq_guard_host`），链接的是 cinux_base 与警告卡。今天文件里登记了 83 个套件（我们数过，数源是 test/CMakeLists.txt 的登记行），不少的套件把内核源文件直接拉进来一起编，调度器、futex、VFS、ext2 都能在咱们自己的电脑上不碰机器地跑，靠的是 [内核库](kernel/lib.md)讲过的断言钩子那一层。test_host 目标把它们一次跑干了，ctest 的并发认环境变量 CINUX_CHECK_JOBS，默认给的是 2。另有一个叫 kernel_image 的 ctest 用例，跑的脚本是 test_kernel_image.cmake，位置在 cmake/test/ 的目录里，拿真实的编译器与 kernel.ld 现编三种布局的迷你镜像，纯代码的一种，带 rodata 的一种，rodata 加 data 的一种，image_size_check 的关也都要过，再把 48 字节的截断版与多塞一个字节的加长版递过去，两个都是必须被拒掉的。image_size_check 自己的准头也有用例看着。

机内的这层叫 ktest（kernel test 的缩写，讲整套基建的是 [内核测试基建](kernel/test.md)，这里只管它怎么编目），编目的文件是 kernel_catalog.cmake，cmake/test/ 是它的家，每条登记是名字:阶段的形状，我们对照文件头注释就能认出这个约定。阶段只有六个合法的值（`exceptions`、`irq`、`keyboard`、`mm`、`proc`、`block`），写的是这个测试内核在用例开跑之前要把初始化阶梯爬到哪一级，一条登记对应的是一个测试文件、一个内核、一张镜像与一次启动四件套。今天的登记数是 22。每场用例在 CTest 里的名字是一条腿，block、smp、smp_pipe 三个是明确要求双核的，双核场跑一遍也就够了，CINUX_KTESTS_DUAL_CPU 把它们限在了 CPU2 一条腿上，其余的各注册 CPU1 与 CPU2 两条腿。

腿上的内核由 add_cinux_ktest 造，造法我们也过一遍。整个 kernel 树为它再编了一遍（挂着 EXCLUDE_FROM_ALL），链接的是 `test/kernel/test_main.cpp` 与 `test/framework/framework_kernel.cpp`，宏 KTEST_STAGE_阶段名 定下了爬梯档位，kallsyms 的桥照走，末了由 add_cinux_image 拼出它自己的镜像。CTest 侧的每条腿是一个用例，名字有着 kernel_test_名字_cpuN 的形状，超时给的是 120 秒，挂着的是 RESOURCE_LOCK cinux_qemu，所以并行的 ctest 到了 QEMU 面前还是会排队。test_kernel 目标按 -j 1 跑全部的机内用例，我们开再大的并发也用不上。

机内用例的判卷人名字是 run_kernel_tests.cmake，它的家安在 cmake/test/，我们把判法读一遍。进门验的是 /dev/kvm，可读可写缺一样的话就 FATAL_ERROR，源码注释写的是只用 -accel kvm，没有 TCG（QEMU 的纯软件模拟后端）的回退。判卷靠的是退出码的协议，源码注释把换算的规则写得清清楚楚，内核经 isa-debug-exit 退了场，交给端口的字节是失败数加一，QEMU 进程的退出码是它的两倍加一，3 的意思是全过，其余的数值能反推失败个数，退出码 1 是没摸到退出端口的早死，挂死另有报的法子。卷面的复核也少不了，串口日志里必须有 `Results: N passed, 0 failed` 的总结，N 的下限是一，空跑与截断的假绿都拦了下来，失败时串口日志整个附在了报错的后面。

## 检查开关与验收矩阵

用例判完了还不算完，检查开关是另一层的保险，全在 cmake/checks/ 的 options.cmake，我们数下来是四个。CINUX_HOST_ASAN 挂的是 ASan 与 UBSan，落的位置在 host 侧，-fno-sanitize-recover=all 的含义是出错即停，泄漏的检查随 ASan 走。CINUX_HOST_TSAN 查的是数据竞争，跟 ASan 是天生的互斥，两个一起开的话就是 FATAL_ERROR，报错文本说得分家住的是两个构建目录，我们想都开就得另起一个目录。CINUX_LOCKDEP 打开自旋锁的顺序检查，host 与内核的两边都生效，开关的值烤成了一个头，模板的名字是 checks_config.hpp.in，家定在了 cmake/checks/，烤出来的产物是 `generated/kernel/proc/checks_config.hpp`，锁顺序怎么记归 [同步原语](kernel/proc/sync.md)那一页。CINUX_UBSAN 管的只是内核，用的是编译器的 trap 模式，踩到未定义行为的话当场就崩在原地，不需要链接宿主的 sanitizer 运行库。

单格的入口是 scripts/checks/ 下的 run-cell.sh，格（cell）就是一次 run-cell 跑下来的验收单元，参数收的是 suite、构建类型、模式与构建目录四样，suite 指的就是下面列出的五个验收范围，我们把模式到开关的翻译看一遍。asan 格开的是 HOST_ASAN 加 LOCKDEP，tsan 格开的是 HOST_TSAN 加 LOCKDEP，ubsan 格开的是 UBSAN，checks 格开的是 UBSAN 加 LOCKDEP，Release 的 C 与 C++ 旗标都被按成了 -O2 -DNDEBUG，Debug 的内核走 -O0。配置完过 test-inventory.py 的清点，脚本的家在 scripts/checks/ 下，名字要跟 test/ctest-baseline.json 的底册对上，登记少了一个的话都不放行。native 格发现构建目录里已有开发盘的话就拒绝运行，日常的盘不能被它覆盖。凡要起 QEMU 的格都要求 /dev/kvm 可读可写。selfhost 与 native 两格把编译器 launcher 清空了，脚本注释说明客人要拿到真实的编译器命令，平时想用 ccache 的话，挂进的通道是 CINUX_COMPILER_LAUNCHER。

五个范围我们按格点菜。host 的菜是 test_host。kernel 的菜是 test_host 与 test_kernel，boot_image 也顺带刷新了。ecosystem 的菜烤的是三样（`boot_image`、`ext2_img`、`qemu_disks`），随后连放了五段的实弹，生态扫描走的是 `test_station33_ecosystem.py`，工具链自检走的是 `test_station34_selfhost.py`，它跟内核自编译的 `test_station34_kernel_selfhost.py` 名字只差了 kernel 一截，验的是 as、ld、GCC、G++ 的产物全是默认 PIE，外网 ping 走的是 `test_net_ping.py`，客人给的是双核，loopback 上的 TCP 走的是 `test_net_tcp.py`，GUI 实弹走的是 `test_station35_gui.py`，--keep 会保留截图的证据。selfhost 的菜编的是 kernel_selfhost_volume，跑的是 station34 的内核自编译脚本。native 的菜烤的是 buildroot_rootfs，随后的实弹按验证分头跑，build 的验证交给的脚本是 test_native_build.py，GUI 的交给 test_native_gui.py，persistence 的验证来两轮，两轮的脚本是同一个 test_persistence.py，第二轮带上了 --gui。

验收的入口是 acceptance.sh，家安在了 scripts/checks/，六格由它串成了一趟，default、asan、tsan、checks 是头里的四格，ecosystem 与 ecosystem-checks 补齐了尾，每格的内部遇错即停，六格走完了再统一汇总，设了 CINUX_CHECK_LOG_DIR 的话成功时也留全套日志。matrix.sh 是同一入口的另一种排法，家定在了 scripts/checks/，格子铺得比六格的密度更大，构建的隔离位置在 build-matrix，我们想把矩阵挪个地方，改的就是 CINUX_MATRIX_DIR，kernel 格还会拿该格自己烤的镜像补跑一遍生态扫描。CI 的分工与排期 scripts/checks/README.md 记着，随 push 与 PR 跑的三项是 `host`、`kernel`、`ecosystem`，selfhost 与 native 的两个长任务，跑在每周六的北京时间 03:17。

scripts/checks 里的工具还有三件，我们也顺带认了。setup-mke2fs.sh 的差事是装 mke2fs，源码的来处是 kernel.org，版本定的是 e2fsprogs 1.47.2，校验是 SHA-256 的，libarchive 也是开着的，装的位置是给定的前缀，末了拿 tar 实测了一把装盘，scripts/checks/README.md 解释了为什么不用发行版自带的 mke2fs，Ubuntu 24.04 的 1.47.0 只认目录输入，Buildroot 那套用户与组的元数据装盘走不通。test-inventory.py 前面说过了。net_decoupling.sh 是两行 grep 的边界检查，kernel/net 不许去 include 驱动与中断层的头，kernel/driver/net 反手抱协议栈的头也不许，谁越了线谁就会被报出来。

scripts/ 根下散着的一群 test_*.py 都是 QEMU 实弹的各家脚本。`test_execve_busybox.py` 验的是 fork、exec 与 SIGCHLD 的回归，跑在盘的副本上。`test_execve_dyn.py` 用键盘敲出动态链接端到端的那一发。`test_gui_tab.py` 验的是桌面终端不抢焦点也能补全。`test_smp.py` 验的是多核，默认打的是 `build/boot/cinux.img`。`test_persistence.py` 在盘的副本上验的是 sync、机内的编译、关机再冷启后数据都在。`test_station35_gui.py` 自起 QEMU 的时候把 USB 的键盘与 tablet 都从设备表里摘掉了，桌面的事件节点走的是 PS/2，证据的存放点是 build/station35/，截图与 monitor 的 socket 都在。mcp/ 目录是它们的公共地基，qemu.py 起 QEMU 并通过 monitor 的 Unix socket 操作，frame.py 解析 P6 格式的截图。mcp 自己的 README 把 GDB 那套写得很全，gdb_server.py 通过 stdio 提供的是 MCP 工具，gdb.py 里的 Session 养着一台冻在复位的 QEMU 与一个常驻的 GDB/MI 会话，读寄存器不用每次重走 file 加 target remote 的仪式，协议的回归用 unittest 跑，跑的命令是 `python3 -m unittest discover -s scripts/mcp -p 'test_gdb*.py'`，真实 GDB 加 QEMU 的集成测试可选开，我们想开的话设 CINUX_GDB_QEMU_TEST=1 就行。

## 代码卫生与资源生成

代码卫生的四件住在 scripts/hooks 里。no_void_cast.sh 用一行 grep 拒收 (void)x 这样的压警告写法，参数该挂上的是 `[[maybe_unused]]`，丢弃返回值的话就直接裸调。clang-tidy.sh 是四件的入口，吃的就是 compile_commands.json，我们配完构建就有了它，缺了就失败，注释写明的是 failing 而非跳过，tidy_inputs.py 把改动过的头映射到编译数据库里真正消费它们的源文件，tidy_database.py 给 clang 改造的是编译命令，GCC 独有的 `-mgeneral-regs-only` 换成了 `-mno-sse`、`-mno-sse2`、`-mno-mmx`（源码注释交代，GCC 允许浮点的声明带这个限制，clang 连 libstdc++ 的声明都不认），报告里出现指向本仓库文件的 error 或 warning 的话，退出码就是非零的了。gen_psf_font.py 能生成 PSF2 格式的字体，8x16 的规格带 256 个字形，用的是 IBM PC 那套 CP437 的位图，内核里嵌的 assets/font.psf 就是这个格式的产物。doxygen/lint.py 是 Doxygen 注释的严格检查器，违例写进了 FAILED_DOXYGEN.md 并以非零码退出。scripts/tags.json 是一份词表的数据，难度、平台、主题三类标签的合法值登记在册。

## 文档站的构建脚本

词表讲完了，文档站自己的构建是另一摊的事，三个 TS 脚本的家安在了 scripts/ 里，package.json 的 scripts 字段是入口。pnpm dev 是开发的入口，API 的数据靠它生成，VitePress 的开发服务器也由它拉起。pnpm build 走的是 scripts/build.ts，文件头的注释把设计讲得很直白，整站按 project.config.ts 声明的卷乘语言拆成了 N 个独立的小 VitePress 构建，并发靠手写的 Promise worker 池，优先级给的是 BUILD_CONCURRENCY 环境变量，其次是 project.config 里的 concurrency，默认给了 4，内容哈希做的是增量缓存，末了把各卷的搜索索引缝起来，跨卷的哈希表重写一遍。链接的检查走 scripts/check-links.ts，用 pnpm check:links 的命令就行，它存在的理由注释里写了，各卷的构建配置里 ignoreDeadLinks 是全开的，断链影响不了构建的成败，这道检查以源码为准补上了缺位，它的速度比扫产物快，还能报出源文件的名称与行号，出了 document 树的相对路径一律判失败。开发日志卷与它的索引生成脚本已在 2026-11-10 随 document/notes 一并移除，站点不再有这一卷。API 的数据（docs:api 那一步）依赖 doxygen，装了就跑，没装的话就提示跳过，我们机器上没有它也不碍着构建。

## 常用命令

日常敲的就这几条，出处都在前面的各节里，我们按出场顺序排好了：

```bash
# 配置，GCC 14 与 CMake 3.16 是根 CMakeLists 划的两道线
cmake -B build -S .
# 跑起来，命令行、GUI、带 GDB 桩三种
cmake --build build --target run -j
cmake --build build --target run_gui -j
cmake --build build --target run_debug -j
# 另开一个终端挂调试器
gdb -q -x build/kernel.gdb
# host 测试与机内测试
cmake --build build --target test_host -j
cmake --build build --target test_kernel -j
# 重烤生态卷，机内测试与生态实弹用的就是它
cmake --build build --target ext2_img -j
# 日常开发盘，Buildroot 烤的 1536 MiB，显式重建会覆盖盘上的工作
cmake --build build --target buildroot_volume -j
# 完整验收，六格串行
CINUX_CHECK_JOBS=4 bash scripts/checks/acceptance.sh
# 本地完整矩阵
CINUX_CHECK_JOBS=4 bash scripts/checks/matrix.sh
# 文档站
pnpm dev
pnpm check:links
```

开发卷上的工作要留着的话，你别去碰 buildroot_volume，run 与 run_gui 都会复用盘里已有的东西。

## 源码位置

- `CMakeLists.txt` — 项目入口，编译器门槛，旗标卡 include 与子目录次序
- `cmake/compilers/` — warning_standard、boot_modes、kernel_modes 三张旗标卡
- `cmake/checks/options.cmake` — ASan、TSan、lockdep、UBSan 四个开关
- `boot/CMakeLists.txt`、`cmake/boot/` — mbr、stage2、lm_blob、尺寸检查与镜像拼装
- `kernel/CMakeLists.txt` — 内核与 ktest 的编译、资源 blob 内嵌、kallsyms 桥
- `cmake/kernel/image_size_check.cmake` — 镜像头尺寸验收
- `cmake/debug/kernel.gdb.in` — run_debug 的 GDB 脚本
- `cmake/qemu/` — 设备清单，run 三目标，scratch 盘，slirp 唤醒库，rootfs 检查
- `cmake/rootfs.cmake` — ext2 生态卷，ktest 副本，自编译卷的总装
- `cmake/test/` — ktest 编目，机内判卷，镜像头用例
- `scripts/create_ext2_disk.sh`、`scripts/build-busybox-glibc.sh`、`scripts/build-toolchain-staging.sh`、`scripts/build-vesper-desk.sh` — 磁盘与用户态的烤制
- `scripts/prepare-kernel-selfhost.py`、`scripts/build-kernel-in-guest.py` — 机内自编译的两端
- `scripts/checks/` — acceptance，matrix，run-cell，test-inventory，net_decoupling，setup-mke2fs
- `scripts/checks/README.md`、`scripts/tags.json` — 验收与 CI 的说明，标签词表
- `scripts/test_*.py` — QEMU 实弹脚本
- `scripts/hooks/`、`scripts/gen_psf_font.py`、`scripts/gen_kallsyms.py`、`scripts/doxygen/lint.py` — 代码卫生与资源生成
- `scripts/mcp/` — GDB MCP 与 QEMU 驾驶的公共地基
- `scripts/build.ts`、`scripts/check-links.ts` — 文档站构建
